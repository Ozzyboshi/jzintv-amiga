#ifndef AVI_AVI_H_
#define AVI_AVI_H_

/* Information and statistics */
typedef struct avi_info_t
{
    uint_64 start_time;
    double  time_scale;
    int     active;
    int     total_audio;
    uint_32 max_size_frame;
    uint_32 max_size_audio;
    int     advance_audio_frames;
    int     frames_per_sec;
    int     usec_per_frame;
    int     audio_rate;
    int     total_frames;
    int     key_frames;
    int     compress;
} avi_info_t;

typedef struct avi_writer_t
{
    struct avi_pvt_t *pvt;
    void             *pvt_alloc;
} avi_writer_t;

// This sets the frame rate scaling.  It's separate from starting a video
// as it's a global parameter for now.
void avi_set_time_scale
(
    const double        time_scale,
    const double        incoming_audio_time_scane
);

int avi_start_video
(
    avi_writer_t *const avi,
    FILE         *const avi_file,
    const int           fps,
    const int           audio_rate,
    const int           compress,
    const uint_64       start_time
);

void avi_set_palette
(
    const avi_writer_t *const avi,
    uint_8 palette[][3],
    const int len
);

void avi_record_audio
(
    const avi_writer_t *const avi,
    const sint_16      *const audio_data,
    const int                 num_samples,
    const int                 silent
);

void avi_record_video
(
    const avi_writer_t *const avi,
    const uint_8       *const image,
    const uint_8              border
);

void avi_end_video(const avi_writer_t *const avi);
int  avi_is_active(const avi_writer_t *const avi);
uint_64 avi_start_time(const avi_writer_t *const avi);

const avi_info_t *avi_info( const avi_writer_t *const avi );

#endif
