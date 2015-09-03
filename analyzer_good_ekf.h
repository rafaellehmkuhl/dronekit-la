#ifndef ANALYZER_GOOD_EKF_H
#define ANALYZER_GOOD_EKF_H

/*
 * analyzer_good_ekf
 *
 */

#include "analyzer.h"

class Analyzer_Good_EKF : public Analyzer {

public:
    Analyzer_Good_EKF(int fd, struct sockaddr_in *sa, AnalyzerVehicle::Base *&vehicle) :
	Analyzer(fd, sa, vehicle),
        seen_ekf_packets(false),
        velocity_variance{
            name: "velocity_variance",
            threshold_warn: 0.5f,
            threshold_fail: 1.0f,
        },
        pos_horiz_variance{
            name: "pos_horiz_variance",
            threshold_warn: 0.5f,
            threshold_fail: 1.0f,
        },
        pos_vert_variance{
            name: "pos_vert_variance",
            threshold_warn: 0.5f,
            threshold_fail: 1.0f,
        },
        compass_variance{
            name: "compass_variance",
            threshold_warn: 0.5f,
            threshold_fail: 1.0f,
        },
        terrain_alt_variance{
            name: "terrain_alt_variance",
            threshold_warn: 0.5f,
            threshold_fail: 1.0f,
        },

        result_velocity_variance{
            variance: &velocity_variance,
            T_start: 0
        },
        result_pos_horiz_variance({
            variance: &pos_horiz_variance,
            T_start: 0
        }),
        result_pos_vert_variance({
            variance: &pos_vert_variance,
            T_start: 0
        }),
        result_compass_variance({
            variance: &compass_variance,
            T_start: 0
        }),
        result_terrain_alt_variance({
            variance: &terrain_alt_variance,
            T_start: 0
        }),

        next_result_variance (0)
    {
    }

    const char *name() { return "Good EKF"; }
    const char *description() {
        return "EKF variances remain within bounds";
    }

    bool configure(INIReader *config);
    void handle_decoded_message(uint64_t T, mavlink_ekf_status_report_t &ekf_status_report) override;
    bool has_failed();


    void results_json_results(Json::Value &root);

private:
    bool seen_ekf_packets;
    
    struct ekf_variance {
        const char *name;
        double threshold_warn;
        double threshold_fail;
    };

    struct ekf_variance velocity_variance;
    struct ekf_variance pos_horiz_variance;
    struct ekf_variance pos_vert_variance;
    struct ekf_variance compass_variance;
    struct ekf_variance terrain_alt_variance;

    struct ekf_variance_result {
        struct ekf_variance *variance;
        uint64_t T_start;
        uint64_t T_stop;
        double max;
    };

    struct ekf_variance_result result_velocity_variance;
    struct ekf_variance_result result_pos_horiz_variance;
    struct ekf_variance_result result_pos_vert_variance;
    struct ekf_variance_result result_compass_variance;
    struct ekf_variance_result result_terrain_alt_variance;

    #define MAX_VARIANCE_RESULTS 100
    uint8_t next_result_variance;
    struct ekf_variance_result result_variance[MAX_VARIANCE_RESULTS];

    void maybe_close_variance_result(struct ekf_variance_result &result);
    void close_variance_result(struct ekf_variance_result &result);
    void end_of_log(uint32_t packet_count) override;

    void handle_variance(uint64_t T,
                         struct ekf_variance &variance,
                         struct ekf_variance_result &result,
                         double value);
    
    void results_json_results_do_variance(Json::Value &root, const struct ekf_variance_result variance_result);
};

#endif


