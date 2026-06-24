#pragma once

/**
@file
@brief Per-frame memory patcher used to implement cheat codes.

This class lives in the emulator core and has no knowledge of cheat code
formats, names, parsing, or the UI's notion of "a cheat" as a user-facing
object. Its only job is: given a list of (address, value, width) patches,
write them all to the SH-2 bus once per frame.

Higher-level concerns — naming, grouping, parsing Action Replay tokens,
saving cheat databases to disk — belong to the frontend, which composes a
flat patch list from its own data model and pushes it down via
`SetActiveCodes()`.

Thread-safety: `SetActiveCodes` and `Clear` may be called from any thread;
they take a mutex. `ApplyAll` takes the same mutex but is only ever called
from the emulator thread (at the top of `Saturn::RunFrameImpl()`), so the
contention is one short critical section per frame.
*/

#include <ymir/core/types.hpp>

#include <ymir/sys/bus.hpp>

#include <mutex>
#include <vector>

namespace ymir::sys {

/// @brief Width of a single patch write.
enum class CheatWidth : uint8 {
    Byte = 1, ///< 8-bit
    Word = 2, ///< 16-bit
    Long = 4, ///< 32-bit
};

/// @brief A single (address, value, width) patch.
struct CheatCode {
    uint32 address = 0;
    uint32 value = 0;
    CheatWidth width = CheatWidth::Word;
};

/// @brief Per-frame memory patcher. Thread-safe.
class CheatEngine {
public:
    CheatEngine() = default;

    /// @brief Replaces the active patch list.
    ///
    /// The frontend computes a flat list of all currently-enabled patches
    /// (expanding multi-code cheats, filtering by master-enable, etc.) and
    /// pushes the result down to the engine each time the UI changes. The
    /// engine just applies whatever it's given.
    void SetActiveCodes(std::vector<CheatCode> codes);

    /// @brief Clears the active patch list. Equivalent to SetActiveCodes({}).
    void Clear();

    /// @brief Number of patches currently held. Thread-safe.
    size_t Count() const;

    /// @brief Applies all active patches to the bus.
    ///
    /// Called by Saturn::RunFrameImpl() at the start of every frame. Holds
    /// the engine's mutex for the duration of the writes (typically a few
    /// dozen Poke<T> calls — well under a microsecond).
    void ApplyAll(SH2Bus &bus) const;

private:
    mutable std::mutex m_mutex;
    std::vector<CheatCode> m_codes;
};

} // namespace ymir::sys
