constexpr double FIXED_32_32_TO_WORLD = 1.0 / 4294967296.0;
constexpr std::size_t VISIBLE_MAP_MARKER_MAX_ENTRIES = 2048;
constexpr DWORD VISIBLE_MAP_MARKER_STALE_MS = 180000;
constexpr DWORD VISIBLE_MAP_MARKER_PRUNE_MS = 2000;
    struct CapturedMapMarkerVisibility
    {
        std::uint64_t raw[5] = {};
        uintptr_t markerAddress = 0;
        uintptr_t stateAddress = 0;
        uintptr_t entityAddress = 0;
        std::int64_t x = 0;
        std::int64_t y = 0;
        std::int64_t z = 0;
        std::uint32_t markerId = 0;
        std::uint32_t markerType = 0;
        std::uint32_t kind = 0;
        std::uint8_t visibility = 0;
        DWORD lastUpdateTick = 0;
        bool hasWorldPosition = false;
    };
    float FixedToWorld(std::int64_t value)
    {
        return static_cast<float>(static_cast<double>(value) * FIXED_32_32_TO_WORLD);
    }
    bool SameVisibleMapMarkerIdentity(const CapturedMapMarkerVisibility& left, const CapturedMapMarkerVisibility& right)
    {
        if (left.stateAddress != 0 && right.stateAddress != 0 && left.stateAddress == right.stateAddress)
            return true;

        if (left.markerAddress != 0 && right.markerAddress != 0 && left.markerAddress == right.markerAddress)
            return true;

        if (!left.hasWorldPosition || !right.hasWorldPosition)
            return false;

        const float dx = FixedToWorld(left.x) - FixedToWorld(right.x);
        const float dz = FixedToWorld(left.z) - FixedToWorld(right.z);
        if (dx * dx + dz * dz > 16.0f)
            return false;

        return left.kind == right.kind || left.markerId == right.markerId;
    }
std::mutex g_visibleMapMarkerMutex; std::vector<CapturedMapMarkerVisibility> g_visibleMapMarkers; DWORD g_lastVisibleMapMarkerPruneTick=0,g_lastVisibleMapMarkerSummaryTick=0; std::atomic<bool> g_debugLoggingEnabled{false};
    void PruneVisibleMapMarkersLocked(DWORD now)
    {
        if (now - g_lastVisibleMapMarkerPruneTick < VISIBLE_MAP_MARKER_PRUNE_MS)
            return;

        g_lastVisibleMapMarkerPruneTick = now;
        g_visibleMapMarkers.erase(
            std::remove_if(g_visibleMapMarkers.begin(), g_visibleMapMarkers.end(), [now](const CapturedMapMarkerVisibility& marker)
            {
                return now - marker.lastUpdateTick > VISIBLE_MAP_MARKER_STALE_MS;
            }),
            g_visibleMapMarkers.end());
    }
    void PublishVisibleMapMarker(CapturedMapMarkerVisibility&& marker)
    {
        const DWORD now = GetTickCount();
        marker.lastUpdateTick = now;

        std::size_t visibleCount = 0;
        std::size_t positionedCount = 0;
        bool changed = false;
        {
            std::lock_guard<std::mutex> lock(g_visibleMapMarkerMutex);
            PruneVisibleMapMarkersLocked(now);

            auto existing = std::find_if(g_visibleMapMarkers.begin(), g_visibleMapMarkers.end(), [&marker](const CapturedMapMarkerVisibility& current)
            {
                return SameVisibleMapMarkerIdentity(current, marker);
            });

            if (existing != g_visibleMapMarkers.end())
            {
                const bool hadPosition = existing->hasWorldPosition;
                if (!marker.hasWorldPosition && hadPosition)
                {
                    marker.x = existing->x;
                    marker.y = existing->y;
                    marker.z = existing->z;
                    marker.hasWorldPosition = true;
                }

                changed = existing->visibility != marker.visibility ||
                    existing->kind != marker.kind ||
                    existing->hasWorldPosition != marker.hasWorldPosition ||
                    existing->x != marker.x ||
                    existing->z != marker.z;
                *existing = marker;
            }
            else if (marker.visibility != 0 && g_visibleMapMarkers.size() < VISIBLE_MAP_MARKER_MAX_ENTRIES)
            {
                g_visibleMapMarkers.push_back(marker);
                changed = true;
            }

            // Counts are used only by the throttled debug summary. Keep cache
            // updates/pruning above; avoid a second full scan for ordinary captures.
            if (!changed || now - g_lastVisibleMapMarkerSummaryTick < 5000 ||
                !g_debugLoggingEnabled.load())
                return;

            for (const CapturedMapMarkerVisibility& current : g_visibleMapMarkers)
            {
                ++summaryVisits;
                if (current.visibility == 0 || now - current.lastUpdateTick > VISIBLE_MAP_MARKER_STALE_MS)
                    continue;

                ++visibleCount;
                if (current.hasWorldPosition)
                    ++positionedCount;
            }
        }

        if (!changed || now - g_lastVisibleMapMarkerSummaryTick < 5000)
            return;

        if (!g_debugLoggingEnabled.load())
            return;

        g_lastVisibleMapMarkerSummaryTick = now;
        std::ostringstream oss;
        oss << "[Minimap] real map marker visibility feed"
            << " | visible=" << visibleCount
            << " | positioned=" << positionedCount
            << " | kind=0x" << std::hex << marker.kind << std::dec
            << " | visibility=" << static_cast<int>(marker.visibility);
        if (marker.hasWorldPosition)
        {
            oss << " | world=("
                << FixedToWorld(marker.x) << ", "
                << FixedToWorld(marker.y) << ", "
                << FixedToWorld(marker.z) << ")";
        }
        Log(oss.str());
    }