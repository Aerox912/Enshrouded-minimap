// Included beside the frame texture recorder so terrain shares its ownership and Vulkan contracts.
    void WriteTerrainFogTile(std::uint8_t* output, int stride, const LiveFogSnapshot& fog, std::size_t tile)
    {
        const auto left = (tile % fog.columns) * 32, top = (tile / fog.columns) * 32;
        for (std::size_t y = 0; y < 32; ++y)
            for (std::size_t x = 0; x < 32; ++x)
                output[((top + y) * stride + left + x) * 4] = fog.tiles[tile * 1024 + y * 32 + x];
    }

    void WriteTerrainFog(std::uint8_t* output, int stride, const LiveFogSnapshot& fog)
    {
        for (std::size_t tile = 0; tile < std::size_t(fog.columns) * fog.rows; ++tile)
            WriteTerrainFogTile(output, stride, fog, tile);
    }

    bool IsTerrainFogValid(const MinimapFogFrame& fog)
    {
        return CurrentMinimapFogSnapshot(fog) && fog.snapshot->columns && fog.snapshot->rows &&
            fog.snapshot->columns <= 128 && fog.snapshot->rows <= 128 &&
            std::isfinite(fog.snapshot->worldWidth) && std::isfinite(fog.snapshot->worldHeight) &&
            fog.snapshot->worldWidth > 0 && fog.snapshot->worldHeight > 0 &&
            fog.snapshot->tiles.size() == std::size_t(fog.snapshot->columns) * fog.snapshot->rows * 1024;
    }

    bool PrepareTerrainTexture(VulkanMinimapRenderer& renderer, std::uint32_t imageIndex,
        void* commandBuffer, const MinimapFogFrame& fog)
    {
        if (renderer.terrainDisabled) { renderer.terrainFallbackReason = "resources-disabled"; return false; }
        if (!renderer.fns.TextureReady() || !renderer.fns.flushMappedMemoryRanges) {
            renderer.terrainDisabled = true;
            renderer.terrainFallbackReason = "functions-unavailable";
            Log("[Minimap] GPU terrain functions unavailable; using CPU terrain until renderer recreation");
            return false;
        }
        EnsureRealMapLoaded();
        std::lock_guard<std::mutex> lock(g_realMapMutex);
        const auto& map = ActiveRealMapLocked();
        if (!map.loaded || map.width <= 0 || map.height <= 0 ||
            map.rgba.size() != std::size_t(map.width) * map.height * 4) { renderer.terrainFallbackReason = "map-data-invalid"; return false; }
        if (renderer.terrainSlots.size() < renderer.commandBuffers.size())
            renderer.terrainSlots.resize(renderer.commandBuffers.size());
        if (imageIndex >= renderer.terrainSlots.size()) { renderer.terrainFallbackReason = "slot-unavailable"; return false; }
        auto& slot = renderer.terrainSlots[imageIndex];
        renderer.terrainImageIndex = imageIndex;
        const bool validFog = IsTerrainFogValid(fog);
        const int fogWidth = validFog ? int(fog.snapshot->columns * 32) : slot.fogWidth;
        const int fogHeight = validFog ? int(fog.snapshot->rows * 32) : slot.fogHeight;
        const int width = MaxValue(map.width, fogWidth), height = map.height + fogHeight;
        // Vulkan guarantees 4096 square textures; larger worlds retain the CPU renderer.
        if (width > 4096 || height > 4096) {
            renderer.terrainFallbackReason = "atlas-too-large";
            if (!renderer.terrainFallbackLogged) {
                renderer.terrainFallbackLogged = true;
                Log("[Minimap] terrain atlas exceeds 4096 pixels; using CPU terrain for this world");
            }
            return false;
        }
        if (!slot.frameTextureReady || slot.mapIdentity != map.rgba.data() || slot.mapWidth != map.width ||
            slot.mapHeight != map.height || slot.fogWidth != fogWidth || slot.fogHeight != fogHeight ||
            (validFog && (!slot.fogSnapshot || slot.fogSnapshot->session != fog.snapshot->session ||
                slot.fogSnapshot->worldWidth != fog.snapshot->worldWidth || slot.fogSnapshot->worldHeight != fog.snapshot->worldHeight)))
        {
            // The caller checked this swapchain image's fence before entering recording.
            DestroyTexturedResources(renderer, slot);
            slot = {};
            std::vector<std::uint8_t> atlas(std::size_t(width) * height * 4, 0);
            for (int y = 0; y < map.height; ++y)
                std::memcpy(atlas.data() + std::size_t(y) * width * 4,
                    map.rgba.data() + std::size_t(y) * map.width * 4, std::size_t(map.width) * 4);
            if (validFog) WriteTerrainFog(atlas.data() + std::size_t(map.height) * width * 4, width, *fog.snapshot);
            if (!TryCreateTexturedResourcesLocked(renderer, slot, atlas, width, height, true))
            {
                renderer.terrainDisabled = true;
                renderer.terrainFallbackReason = "resource-creation-failed";
                Log("[Minimap] GPU terrain unavailable; using CPU terrain until renderer recreation");
                return false;
            }
            slot.mapIdentity = map.rgba.data(); slot.mapWidth = map.width; slot.mapHeight = map.height;
            slot.fogWidth = fogWidth; slot.fogHeight = fogHeight;
            slot.fogSnapshot = validFog ? fog.snapshot : nullptr;
        }
        else if (validFog && slot.fogSnapshot != fog.snapshot)
        {
            slot.fogDirtyRegions.clear();
            for (std::size_t tile = 0; tile < std::size_t(fog.snapshot->columns) * fog.snapshot->rows; ++tile)
            {
                if (std::memcmp(slot.fogSnapshot->tiles.data() + tile * 1024,
                    fog.snapshot->tiles.data() + tile * 1024, 1024) == 0) continue;
                VkBufferImageCopy copy{};
                copy.bufferRowLength = width;
                copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                copy.imageSubresource.layerCount = 1;
                copy.imageOffset.x = static_cast<std::int32_t>((tile % fog.snapshot->columns) * 32);
                copy.imageOffset.y = map.height + static_cast<std::int32_t>((tile / fog.snapshot->columns) * 32);
                copy.bufferOffset = (std::uint64_t(copy.imageOffset.y) * width + copy.imageOffset.x) * 4;
                copy.imageExtent = {32, 32, 1};
                slot.fogDirtyRegions.push_back(copy);
            }
            if (!slot.fogDirtyRegions.empty())
            {
                void* mapped = nullptr;
                void* device = reinterpret_cast<void*>(renderer.device);
                void* memory = reinterpret_cast<void*>(slot.frameStagingMemory);
                if (renderer.fns.mapMemory(device, memory, 0, ~std::uint64_t(0), 0, &mapped) != VK_SUCCESS || !mapped)
                { renderer.terrainFallbackReason = "fog-map-failed"; return false; }
                auto* output = static_cast<std::uint8_t*>(mapped) + std::size_t(map.height) * width * 4;
                for (const auto& region : slot.fogDirtyRegions)
                {
                    const auto tile = std::size_t((region.imageOffset.y - map.height) / 32) * fog.snapshot->columns + region.imageOffset.x / 32;
                    WriteTerrainFogTile(output, width, *fog.snapshot, tile);
                }
                // Whole-allocation flush keeps noncoherent atom alignment; conversion and copies stay tile-local.
                VkMappedMemoryRange range{}; range.memory = memory;
                const auto result = renderer.fns.flushMappedMemoryRanges(device, 1, &range);
                renderer.fns.unmapMemory(device, memory);
                if (result != VK_SUCCESS) { renderer.terrainFallbackReason = "fog-flush-failed"; return false; }
                slot.fogUploadPending = true;
            }
            slot.fogSnapshot = fog.snapshot;
        }
        if (slot.frameTextureUploadPending)
        {
            RecordTextureUpload(renderer, slot, commandBuffer);
            renderer.terrainUploadBytes += std::uint64_t(width) * height * 4;
            ++renderer.terrainUploadRegions;
        }
        else if (slot.fogUploadPending)
        {
            RecordTextureUpload(renderer, slot, commandBuffer, false, 0, &slot.fogDirtyRegions);
            renderer.terrainUploadBytes += std::uint64_t(slot.fogDirtyRegions.size()) * 32 * 32 * 4;
            renderer.terrainUploadRegions += static_cast<std::uint32_t>(slot.fogDirtyRegions.size());
        }
        slot.fogUploadPending = false;
        if (!renderer.terrainInitializedLogged) {
            renderer.terrainInitializedLogged = true;
            Log("[Minimap] GPU terrain active | atlas=" + std::to_string(width) + "x" + std::to_string(height) +
                " | slots=" + std::to_string(renderer.commandBuffers.size()) + " | staging_plus_image_bytes_per_slot=" +
                std::to_string(std::uint64_t(width) * height * 8));
        }
        return true;
    }

    bool TryDrawTerrainTexture(VulkanMinimapRenderer& renderer, void* commandBuffer, int cx, int cy, int radius,
        float centerX, float centerZ, float unitsPerPixel, float headingRadians, const MinimapFogFrame& fog)
    {
        if (renderer.terrainDisabled) { renderer.terrainFallbackReason = "resources-disabled"; return false; }
        if (renderer.terrainImageIndex >= renderer.terrainSlots.size()) { renderer.terrainFallbackReason = "slot-unavailable"; return false; }
        const auto& slot = renderer.terrainSlots[renderer.terrainImageIndex];
        if (!slot.frameTextureReady || slot.frameTextureUploadPending || slot.fogUploadPending)
        { renderer.terrainFallbackReason = "texture-not-ready"; return false; }
        const bool validFog = IsTerrainFogValid(fog);
        // A failed refresh must not draw a previous world's exploration mask.
        if (validFog && slot.fogSnapshot != fog.snapshot) { renderer.terrainFallbackReason = "fog-not-synchronized"; return false; }
        int left = cx - radius, top = cy - radius, width = 2 * radius + 1, height = width;
        ScaleMinimapRect(renderer, left, top, width, height);
        const float push[32] = {
            2.f * left / renderer.width - 1, 2.f * top / renderer.height - 1,
            2.f * (left + width) / renderer.width - 1, 2.f * (top + height) / renderer.height - 1,
            centerX, centerZ, unitsPerPixel, float(radius),
            std::cos(headingRadians), std::sin(headingRadians), float(ClampValue(g_minimapMapSampleStep.load(), 1, 4)),
            float(ClampValue(g_minimapMapLight.load(), 0, 100)),
            float(slot.mapWidth), float(slot.mapHeight), float(slot.frameTextureWidth), float(slot.frameTextureHeight),
            float(slot.fogWidth), float(slot.fogHeight), validFog ? fog.snapshot->worldWidth : 0,
            validFog ? fog.snapshot->worldHeight : 0,
            fog.enabled ? 1.f : 0.f, validFog ? 1.f : 0.f,
            renderer.colorSpace == 1000104002 ? 1.f : renderer.colorSpace == 1000104008 ? 2.f : 0.f, 0.f,
            float(renderer.widgetCenterX), float(renderer.widgetCenterY), renderer.widgetScale, 0.f,
            float(cx - radius), float(cy - radius), 0.f, IsSquareMinimap(renderer) ? 1.f : 0.f
        };
        void* set = reinterpret_cast<void*>(slot.frameDescriptorSet);
        void* layout = reinterpret_cast<void*>(slot.framePipelineLayout);
        renderer.fns.cmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, reinterpret_cast<void*>(slot.framePipeline));
        renderer.fns.cmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &set, 0, nullptr);
        renderer.fns.cmdPushConstants(commandBuffer, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(push), push);
        renderer.fns.cmdDraw(commandBuffer, 6, 1, 0, 0);
        renderer.terrainFallbackReason = nullptr;
        return true;
    }
