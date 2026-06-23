/*
 * SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace sdr {

// Abstract source of spectrum magnitude frames.
//
// A frame is `n_bins` magnitudes normalized to [0, 1], laid out from the low
// edge of the displayed band to the high edge (left to right on screen).
//
// Concrete implementations: MockSpectrumSource (below) for development; later a
// live RTL-SDR tap feeding real FFT bins. The UI only depends on this interface,
// so swapping the backend is a one-line change.
class SpectrumSource {
public:
    virtual ~SpectrumSource() = default;

    // Fill `mags` with `n_bins` values in [0, 1]. Must not block.
    virtual void next_frame(float* mags, int n_bins) = 0;

    // Peak magnitude of the most recent frame, in [0, 1]; the screen maps this
    // to the S-meter. Default 0 for sources that don't track it.
    virtual float last_peak() const { return 0.0f; }

    // Tell the source the tuned centre frequency (Hz) and the visible span (Hz)
    // so a real receiver can retune the hardware and crop the FFT to the zoom
    // window. Called from the UI tick. Default no-op for synthetic sources.
    virtual void set_tuning(int64_t /*center_hz*/, int32_t /*span_hz*/) {}

    // Demodulation mode for audio (index matches sdr::RadioMode:
    // 0=WFM 1=FM 2=AM 3=USB 4=LSB 5=CW). Default no-op.
    virtual void set_mode(int /*mode*/) {}

    // Audio master volume [0,1] and mute. Default no-op (no audio path).
    virtual void set_volume(float /*vol*/) {}
    virtual void set_muted(bool /*muted*/) {}

    // Receiver gain: automatic (tuner AGC) or manual gain in 0.1 dB. Default
    // no-op for sources without tunable gain.
    virtual void set_gain(bool /*automatic*/, int /*tenth_db*/) {}
};

// Synthetic source: low random noise floor plus a couple of gaussian "carriers"
// that drift slowly in frequency, so the waterfall clearly shows signals moving
// — proof the pipeline is live.
class MockSpectrumSource final : public SpectrumSource {
public:
    explicit MockSpectrumSource(uint32_t seed = 0x5D2A11C3u);

    void next_frame(float* mags, int n_bins) override;

    float last_peak() const override { return last_peak_; }

private:
    float frand();

    uint32_t rng_state_;
    float    phase_ = 0.0f;   // advances every frame to drive carrier drift
    float    last_peak_ = 0.0f;
};

} // namespace sdr
