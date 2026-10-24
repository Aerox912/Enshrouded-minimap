// Exercises the actual private Minimap admission/lifecycle helpers against inert fixture data.
#include <windows.h>
#include "../src/client_1_0_profile.h"
#include "../src/renderer_ownership_adapter.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

namespace
{
    using RendererOwnershipAdapter::MenuAdmission;
    using native_ui_host::renderer_ownership::Api;
    using native_ui_host::renderer_ownership::Proof;
    using native_ui_host::renderer_ownership::Status;

    struct FakeQueryState
    {
        Status status = Status::verified;
        Proof proof{};
        bool writeProof = true;
        unsigned calls = 0;
        bool outputWasZeroOnEntry = false;
    };

    FakeQueryState* currentQuery = nullptr;

    Status FakeQuery(Proof* output, std::uint32_t outputSize)
    {
        if (!currentQuery) return Status::invalid_request;
        ++currentQuery->calls;
        bool zero = output != nullptr;
        if (output)
            for (const auto value : *reinterpret_cast<const std::uint8_t(*)[sizeof(Proof)]>(output))
                zero = zero && value == 0;
        currentQuery->outputWasZeroOnEntry = zero;
        if (!output || outputSize != sizeof(Proof)) return Status::invalid_request;
        if (currentQuery->writeProof) *output = currentQuery->proof;
        return currentQuery->status;
    }

    void Check(bool ok, const char* message)
    {
        if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
        std::cout << "PASS: " << message << '\n';
    }

    std::vector<std::uint8_t> MapFixture(const char* path)
    {
        Check(MinimapClient10::MatchesExactFile(path), "fixture has the exact current executable SHA256");
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        Check(input.good(), "fixture opens read only");
        std::vector<std::uint8_t> file(static_cast<std::size_t>(input.tellg()));
        input.seekg(0);
        Check(static_cast<bool>(input.read(reinterpret_cast<char*>(file.data()), file.size())), "fixture bytes read completely");
        Check(file.size() >= sizeof(IMAGE_DOS_HEADER), "fixture contains a DOS header");
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(file.data());
        Check(dos->e_magic == IMAGE_DOS_SIGNATURE && dos->e_lfanew >= 0 &&
            static_cast<std::size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS64) <= file.size(),
            "fixture PE header bounds hold");
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(file.data() + dos->e_lfanew);
        Check(nt->Signature == IMAGE_NT_SIGNATURE && nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC,
            "fixture is a 64-bit PE image");
        std::vector<std::uint8_t> image(nt->OptionalHeader.SizeOfImage);
        const auto* sections = IMAGE_FIRST_SECTION(nt);
        for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
            const auto& section = sections[i];
            Check(static_cast<std::size_t>(section.PointerToRawData) + section.SizeOfRawData <= file.size() &&
                static_cast<std::size_t>(section.VirtualAddress) + section.SizeOfRawData <= image.size(),
                "PE section bounds hold");
            std::memcpy(image.data() + section.VirtualAddress, file.data() + section.PointerToRawData, section.SizeOfRawData);
        }
        return image;
    }

    void SetValidProof(FakeQueryState& fake, const std::vector<std::uint8_t>& image)
    {
        fake.status = Status::verified;
        fake.writeProof = true;
        fake.proof = {};
        fake.proof.image_base = reinterpret_cast<std::uintptr_t>(image.data());
        fake.proof.renderer_rva = native_ui_host::renderer_ownership::renderer_rva;
        fake.proof.prefix_size = native_ui_host::renderer_ownership::original_prefix_size;
        std::memcpy(fake.proof.original_prefix, image.data() + MinimapMenu10::RendererRva,
            sizeof(fake.proof.original_prefix));
    }

    void CheckAdmission(const std::vector<std::uint8_t>& original)
    {
        auto image = original;
        FakeQueryState fake{};
        currentQuery = &fake;
        Api api{sizeof(Api), native_ui_host::renderer_ownership::abi_version, &FakeQuery};
        const auto base = reinterpret_cast<std::uintptr_t>(image.data());
        Check(RendererOwnershipAdapter::EvaluateMenuGuards(image.data(), image.size(), base, &api) == MenuAdmission::raw,
            "complete raw guards admit standalone Minimap");
        Check(fake.calls == 0, "raw admission never queries a provider");

        api.size = 0;
        Check(RendererOwnershipAdapter::EvaluateMenuGuards(image.data(), image.size(), 0, nullptr) == MenuAdmission::raw,
            "raw admission does not require provider ABI or base proof");
        api.size = sizeof(Api);

        SetValidProof(fake, image);
        std::memset(image.data() + MinimapMenu10::RendererRva, 0xCC,
            native_ui_host::renderer_ownership::original_prefix_size);
        fake.calls = 0;
        Check(RendererOwnershipAdapter::EvaluateMenuGuards(image.data(), image.size(), base, &api) == MenuAdmission::owned,
            "verified provider proof admits exactly the patched renderer entry");
        Check(fake.calls == 1 && fake.outputWasZeroOnEntry,
            "owned admission makes one fresh query with zero-initialized output");

        const auto validProof = fake.proof;
        fake.proof.image_base++;
        Check(RendererOwnershipAdapter::EvaluateMenuGuards(image.data(), image.size(), base, &api) == MenuAdmission::rejected,
            "wrong image base cannot authorize entry bytes");
        fake.proof = validProof;
        fake.proof.renderer_rva++;
        Check(RendererOwnershipAdapter::EvaluateMenuGuards(image.data(), image.size(), base, &api) == MenuAdmission::rejected,
            "wrong renderer RVA cannot authorize entry bytes");
        fake.proof = validProof;
        fake.proof.prefix_size--;
        Check(RendererOwnershipAdapter::EvaluateMenuGuards(image.data(), image.size(), base, &api) == MenuAdmission::rejected,
            "wrong prefix length cannot authorize entry bytes");
        fake.proof = validProof;
        fake.proof.reserved[0] = 1;
        Check(RendererOwnershipAdapter::EvaluateMenuGuards(image.data(), image.size(), base, &api) == MenuAdmission::rejected,
            "nonzero reserved proof bytes are rejected");
        fake.proof = validProof;
        fake.proof.original_prefix[0] ^= 1;
        Check(RendererOwnershipAdapter::EvaluateMenuGuards(image.data(), image.size(), base, &api) == MenuAdmission::rejected,
            "mismatched expected renderer prefix is rejected");
        fake.proof = validProof;
        Check(RendererOwnershipAdapter::EvaluateMenuGuards(image.data(), image.size(), base + 1, &api) == MenuAdmission::rejected,
            "proof lookup requires the actual image base");

        Api wrongApiSize = api;
        wrongApiSize.size--;
        Check(RendererOwnershipAdapter::EvaluateMenuGuards(image.data(), image.size(), base, &wrongApiSize) == MenuAdmission::rejected,
            "wrong API size is rejected before querying");
        Api wrongApiVersion = api;
        wrongApiVersion.abi++;
        Check(RendererOwnershipAdapter::EvaluateMenuGuards(image.data(), image.size(), base, &wrongApiVersion) == MenuAdmission::rejected,
            "wrong API ABI is rejected before querying");
        Api missingQuery = api;
        missingQuery.query = nullptr;
        Check(RendererOwnershipAdapter::EvaluateMenuGuards(image.data(), image.size(), base, &missingQuery) == MenuAdmission::rejected,
            "missing query function is rejected before querying");

        fake.proof = {};
        fake.writeProof = false;
        fake.status = Status::busy;
        Check(RendererOwnershipAdapter::EvaluateMenuGuards(image.data(), image.size(), base, &api) == MenuAdmission::busy,
            "provider busy is transient and never acceptance");
        Check(fake.outputWasZeroOnEntry, "busy query receives fresh zero-initialized proof storage");
        fake.status = Status::uninstalled;
        Check(RendererOwnershipAdapter::EvaluateMenuGuards(image.data(), image.size(), base, &api) == MenuAdmission::rejected,
            "uninstalled zero proof cannot authorize entry bytes");
        for (const auto statusValue : {3u, 4u, 5u, 6u, 0xffffffffu}) {
            fake.status = static_cast<Status>(statusValue);
            Check(RendererOwnershipAdapter::EvaluateMenuGuards(image.data(), image.size(), base, &api) == MenuAdmission::rejected,
                "partial tampered invalid and unknown statuses are rejected");
        }

        fake.status = Status::verified;
        fake.writeProof = true;
        fake.proof = validProof;
        image[0xc4e0d2] ^= 1;
        Check(RendererOwnershipAdapter::EvaluateMenuGuards(image.data(), image.size(), base, &api) == MenuAdmission::rejected,
            "renderer tail guard remains required");
        image[0xc4e0d2] ^= 1;
        image[0xbc0446] ^= 1;
        Check(RendererOwnershipAdapter::EvaluateMenuGuards(image.data(), image.size(), base, &api) == MenuAdmission::rejected,
            "caller guard remains required");
        image[0xbc0446] ^= 1;
        image[0xd6ade0] ^= 1;
        Check(RendererOwnershipAdapter::EvaluateMenuGuards(image.data(), image.size(), base, &api) == MenuAdmission::rejected,
            "layout guard remains required");
        image[0xd6ade0] ^= 1;
        Check(RendererOwnershipAdapter::EvaluateMenuGuards(nullptr, image.size(), base, &api) == MenuAdmission::rejected,
            "null image is rejected");
        Check(RendererOwnershipAdapter::EvaluateMenuGuards(image.data(), 0, base, &api) == MenuAdmission::rejected,
            "empty image is rejected");
        currentQuery = nullptr;
    }

    void CheckLifecycle()
    {
        bool pending = false;
        std::atomic<bool> everPublished{false};
        RendererOwnershipAdapter::RecordInitialMenuAdmission(pending, MenuAdmission::busy);
        Check(pending, "Load busy alone marks the first menu attempt pending");
        for (const auto admission : {MenuAdmission::raw, MenuAdmission::owned, MenuAdmission::rejected}) {
            pending = true;
            RendererOwnershipAdapter::RecordInitialMenuAdmission(pending, admission);
            Check(!pending, "only a busy admission marks the first menu attempt pending");
        }

        unsigned evaluations = 0;
        unsigned preparations = 0;
        unsigned activations = 0;
        auto evaluate = [&]() {
            ++evaluations;
            return evaluations == 1 ? MenuAdmission::busy : MenuAdmission::owned;
        };
        auto prepare = [&]() { ++preparations; return true; };
        auto activate = [&](bool retained, MenuAdmission admission) {
            ++activations;
            Check(!retained && admission == MenuAdmission::owned, "first publication uses fresh owned admission");
            everPublished.store(true, std::memory_order_release);
            return true;
        };

        Check(!RendererOwnershipAdapter::ActivateMenuAtBoundary(
            pending, everPublished, evaluate, prepare, activate), "Activate busy leaves the callsite unpublished");
        Check(pending && evaluations == 1 && preparations == 1 && activations == 0,
            "busy Activate prepares the unpublished callsite but never publishes it");
        Check(!RendererOwnershipAdapter::RetryPendingMenuAtUpdate(
            false, pending, everPublished, evaluate, prepare, activate),
            "inactive mod does not consume pending admission");
        Check(pending && evaluations == 1, "inactive update performs no provider query");
        Check(RendererOwnershipAdapter::RetryPendingMenuAtUpdate(
            true, pending, everPublished, evaluate, prepare, activate),
            "active Update recovers once from natural provider readiness");
        Check(!pending && everPublished.load() && evaluations == 2 && preparations == 2 && activations == 1,
            "successful update reuses its prepared callsite and publishes only once");
        Check(!RendererOwnershipAdapter::RetryPendingMenuAtUpdate(
            true, pending, everPublished, evaluate, prepare, activate) && evaluations == 2,
            "published callsite is never retried by Update");

        bool rejectedPending = true;
        RendererOwnershipAdapter::RecordInitialMenuAdmission(rejectedPending, MenuAdmission::rejected);
        unsigned rejectedEvaluations = 0;
        auto rejectedEvaluate = [&]() { ++rejectedEvaluations; return MenuAdmission::owned; };
        Check(!rejectedPending && !RendererOwnershipAdapter::RetryPendingMenuAtUpdate(
            true, rejectedPending, everPublished, rejectedEvaluate, prepare, activate) && rejectedEvaluations == 0,
            "terminal rejection clears pending and suppresses Update retry");

        bool partialPending = false;
        std::atomic<bool> partialEverPublished{false};
        unsigned partialEvaluations = 0;
        bool retainedCheckCalled = false;
        auto partialEvaluate = [&]() { ++partialEvaluations; return MenuAdmission::owned; };
        auto partialActivate = [&](bool retained, MenuAdmission) {
            if (retained) { retainedCheckCalled = true; return false; }
            partialEverPublished.store(true, std::memory_order_release);
            return false;
        };
        Check(!RendererOwnershipAdapter::ActivateMenuAtBoundary(
            partialPending, partialEverPublished, partialEvaluate, prepare, partialActivate),
            "publication-finalization failure remains a failed activation");
        Check(!partialPending && partialEverPublished.load() && partialEvaluations == 1,
            "partial publication clears pending and retains ownership state");
        Check(!RendererOwnershipAdapter::RetryPendingMenuAtUpdate(
            true, partialPending, partialEverPublished, partialEvaluate, prepare, partialActivate) && partialEvaluations == 1,
            "partial publication is never retried or reevaluated by Update");
        Check(!RendererOwnershipAdapter::ActivateMenuAtBoundary(
            partialPending, partialEverPublished, partialEvaluate, prepare, partialActivate) &&
            retainedCheckCalled && partialEvaluations == 1,
            "later Activate checks retained ownership without fresh admission or replacement");
    }
}

int main(int argc, char** argv)
{
    Check(argc == 2, "supply one offline executable fixture");
    const auto image = MapFixture(argv[1]);
    CheckLifecycle();
    CheckAdmission(image);
}
