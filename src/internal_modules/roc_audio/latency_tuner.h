/*
 * Copyright (c) 2017 Roc Streaming authors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

//! @file roc_audio/latency_tuner.h
//! @brief Latency tuner.

#ifndef ROC_AUDIO_LATENCY_TUNER_H_
#define ROC_AUDIO_LATENCY_TUNER_H_

#include "roc_audio/freq_estimator.h"
#include "roc_audio/sample_spec.h"
#include "roc_core/noncopyable.h"
#include "roc_core/optional.h"
#include "roc_core/time.h"
#include "roc_status/status_code.h"

namespace roc {
namespace audio {

//! Latency tuner backend.
//! Defines which latency we monitor and tune to achieve target.
enum LatencyTunerBackend {
    //! Deduce best default for given settings.
    LatencyTunerBackend_Default,

    //! Latency is Network Incoming Queue length.
    //! Calculated on receiver without use of any signaling protocol.
    //! Reported back to sender via RTCP XR.
    LatencyTunerBackend_Niq,

    //! Latency is End-to-end delay.
    //! Can on receiver if RTCP XR is supported by both sides.
    //! Reported back to sender via RTCP XR.
    LatencyTunerBackend_E2e
};

//! Latency tuner profile.
//! Defines whether and how we tune latency on fly to compensate clock
//! drift and jitter.
enum LatencyTunerProfile {
    //! Deduce best default for given settings.
    LatencyTunerProfile_Default,

    //! Do not tune latency.
    LatencyTunerProfile_Intact,

    //! Fast and responsive tuning.
    //! Good for lower network latency and jitter.
    LatencyTunerProfile_Responsive,

    //! Slow and smooth tuning.
    //! Good for higher network latency and jitter.
    LatencyTunerProfile_Gradual
};

//! Latency settings.
struct LatencyConfig {
    //! Latency tuner backend to use.
    LatencyTunerBackend tuner_backend;

    //! Latency tuner profile to use.
    LatencyTunerProfile tuner_profile;

    //! Target latency.
    //! If zero, latency tuning should be disabled, otherwise an error occurs.
    //! If negative, default value is used if possible.
    core::nanoseconds_t target_latency;

    //! Maximum allowed deviation from target latency.
    //! If the latency goes out of bounds, the session is terminated.
    //! If zero, bounds checks are disabled.
    //! If negative, default value is used if possible.
    core::nanoseconds_t latency_tolerance;

    //! Maximum delay since last packet before queue is considered stalling.
    //! If niq_stalling becomes larger than stalling_tolerance, latency
    //! tolerance checks are temporary disabled.
    //! If zero, stalling checks are disabled.
    //! If negative, default value is used if possible.
    core::nanoseconds_t stale_tolerance;

    //! Scaling update interval.
    //! How often to run FreqEstimator and update Resampler scaling.
    core::nanoseconds_t scaling_interval;

    //! Maximum allowed deviation of freq_coeff from 1.0.
    //! If the scaling goes out of bounds, it is trimmed.
    //! For example, 0.01 allows freq_coeff values in range [0.99; 1.01].
    float scaling_tolerance;

    //! Initialize.
    LatencyConfig()
        : tuner_backend(LatencyTunerBackend_Default)
        , tuner_profile(LatencyTunerProfile_Default)
        , target_latency(-1)
        , latency_tolerance(-1)
        , stale_tolerance(-1)
        , scaling_interval(5 * core::Millisecond)
        , scaling_tolerance(0.005f) {
    }

    //! Automatically fill missing settings.
    void deduce_defaults(core::nanoseconds_t default_target_latency, bool is_receiver);
};

//! Latency metrics.
struct LatencyMetrics {
    //! Estimated network incoming queue latency.
    //! An estimate of how much media is buffered in receiver packet queue.
    core::nanoseconds_t niq_latency;

    //! Delay since last received packet.
    //! In other words, how long there were no new packets in network incoming queue.
    core::nanoseconds_t niq_stalling;

    //! Estimated end-to-end latency.
    //! An estimate of the time from recording a frame on sender to playing it
    //! on receiver.
    core::nanoseconds_t e2e_latency;

    //! Estimated interarrival jitter.
    //! An estimate of the statistical variance of the RTP data packet
    //! interarrival time.
    core::nanoseconds_t jitter;

    LatencyMetrics()
        : niq_latency(0)
        , niq_stalling(0)
        , e2e_latency(0)
        , jitter(0) {
    }
};

//! Latency tuner.
//!
//! On receiver, LatencyMonitor computes local metrics and passes them to LatencyTuner.
//! On sender, FeedbackMonitor obtains remote metrics and passes them to LatencyTuner.
//! In both cases, LatencyTuner processes metrics and computes scaling factor that
//! should be passed to resampler.
//!
//! Features:
//! - monitors how close actual latency and target latency are
//! - monitors whether latency goes out of bounds
//! - assuming that the difference between actual latency and target latency is
//!   caused by the clock drift between sender and receiver, calculates scaling
//!   factor for resampler to compensate it
class LatencyTuner : public core::NonCopyable<> {
public:
    //! Initialize.
    LatencyTuner(const LatencyConfig& config, const SampleSpec& sample_spec);

    //! Check if the object was initialized successfully.
    bool is_valid() const;

    //! Pass updated metrics to tuner.
    //! Tuner will use new metrics next time when advance() is called.
    void write_metrics(const LatencyMetrics& metrics);

    //! Advance stream by given number of samples.
    //! This method performs all actual work:
    //!  - depending on configured backend, selects which latency from
    //!    metrics to use
    //!  - check if latency goes out of bounds and session should be
    //!    terminated; if so, returns false
    //!  - computes updated scaling based on latency history and configured
    //!    profile
    bool advance_stream(size_t n_samples);

    //! Get computed scaling.
    //! Latency tuner expects that this scaling will applied to the stream
    //! resampler, so that the latency will slowly achieve target value.
    //! Returned value is close to 1.0.
    //! If no scaling was computed yet, it returns 0.0.
    float get_scaling() const;

private:
    bool update_();

    bool check_bounds_(packet::stream_timestamp_diff_t latency);
    void compute_scaling_(packet::stream_timestamp_diff_t latency);

    void report_();

    core::Optional<FreqEstimator> fe_;

    packet::stream_timestamp_t stream_pos_;

    const packet::stream_timestamp_t update_interval_;
    packet::stream_timestamp_t update_pos_;

    const packet::stream_timestamp_t report_interval_;
    packet::stream_timestamp_t report_pos_;

    float freq_coeff_;
    const float freq_coeff_max_delta_;

    const LatencyTunerBackend backend_;
    const LatencyTunerProfile profile_;

    const bool enable_checking_;
    const bool enable_tuning_;

    bool has_niq_latency_;
    packet::stream_timestamp_diff_t niq_latency_;
    packet::stream_timestamp_diff_t niq_stalling_;

    bool has_e2e_latency_;
    packet::stream_timestamp_diff_t e2e_latency_;

    bool has_jitter_;
    packet::stream_timestamp_diff_t jitter_;

    packet::stream_timestamp_diff_t target_latency_;
    packet::stream_timestamp_diff_t min_latency_;
    packet::stream_timestamp_diff_t max_latency_;
    packet::stream_timestamp_diff_t max_stalling_;

    const SampleSpec sample_spec_;

    bool valid_;
};

//! Get string name of latency backend.
const char* latency_tuner_backend_to_str(LatencyTunerBackend backend);

//! Get string name of latency tuner.
const char* latency_tuner_profile_to_str(LatencyTunerProfile tuner);

} // namespace audio
} // namespace roc

#endif // ROC_AUDIO_LATENCY_TUNER_H_