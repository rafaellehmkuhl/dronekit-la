#ifndef IMAGETAGGER_H
#define IMAGETAGGER_H

/*
 * imagetagger
 *
 */

#include "INIReader.h"

#include "mavlink_message_handler.h"
#include "mavlink_reader.h"

#include "analyze.h"
#include "common_tool.h"

#include <vector>

class ImageTagger : Common_Tool {

public:
    ImageTagger() :
        Common_Tool(),
        _pathname(NULL),
        _imagedir(NULL),
        _time_offset_string(NULL),
        _time_offset(0)
        { }

    class Image_Info {
    public:
        Image_Info() :
            latitude(0.0f),
            longitude(0.0f),
            altitude(0.0f),
            have_gps_raw_int(false),
            gimbal_pitch(0.0f),
            gimbal_roll(0.0f),
            gimbal_yaw(0.0f),
            have_mount_status(false)
            { }
        // FIXME: scope
        std::string path;
        uint64_t timestamp;

        // variables that get filled in:
        double latitude;
        double longitude;
        double altitude; // metres
        bool have_gps_raw_int;

        double gimbal_pitch;
        double gimbal_roll;
        double gimbal_yaw;
        bool have_mount_status;

        bool have_all_data() {
            return (have_gps_raw_int && have_mount_status);
        }
        bool operator < (const ImageTagger::Image_Info x) const {
            return (timestamp < x.timestamp);
        }
    };

    std::vector<class Image_Info*> get_image_info(const char *dirpath);

    void parse_filepath(const char *filepath);
    void run();

    void parse_arguments(int argc, char *argv[]);

private:
    char *_pathname;
    char *_imagedir;
    char *_time_offset_string;

    int64_t _time_offset;

    MAVLink_Reader *reader;
    long _argc;
    char **_argv;
    const char *program_name();

    void do_tagging(Image_Info *info);
    
    void usage();
    void instantiate_message_handlers(INIReader *config,
                                      int fd_telem_forwarder,
                                      struct sockaddr_in *sa_tf);
};

#endif
