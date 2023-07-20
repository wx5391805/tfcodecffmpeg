#include "tfenc.h"
#include "nvenc.h"

#include "libavutil/internal.h"

#include "avcodec.h"
#include "encode.h"
#include "internal.h"
#include "packet_internal.h"
#define OFFSET(x) offsetof(NvencContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
#ifdef NVENC_HAVE_NEW_PRESETS
    { "preset",       "Set the encoding preset",            OFFSET(preset),       AV_OPT_TYPE_INT,   { .i64 = PRESET_P4 },     PRESET_DEFAULT, PRESET_P7,          VE, "preset" },
#else
    { "preset",       "Set the encoding preset",            OFFSET(preset),       AV_OPT_TYPE_INT,   { .i64 = PRESET_MEDIUM }, PRESET_DEFAULT, PRESET_LOSSLESS_HP, VE, "preset" },
#endif
    { "default",      "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = PRESET_DEFAULT },             0, 0, VE, "preset" },
    { "slow",         "hq 2 passes",                        0,                    AV_OPT_TYPE_CONST, { .i64 = PRESET_SLOW },                0, 0, VE, "preset" },
    { "medium",       "hq 1 pass",                          0,                    AV_OPT_TYPE_CONST, { .i64 = PRESET_MEDIUM },              0, 0, VE, "preset" },
    { "fast",         "hp 1 pass",                          0,                    AV_OPT_TYPE_CONST, { .i64 = PRESET_FAST },                0, 0, VE, "preset" },
    { "hp",           "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = PRESET_HP },                  0, 0, VE, "preset" },
    { "hq",           "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = PRESET_HQ },                  0, 0, VE, "preset" },
    { "bd",           "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = PRESET_BD },                  0, 0, VE, "preset" },
    { "ll",           "low latency",                        0,                    AV_OPT_TYPE_CONST, { .i64 = PRESET_LOW_LATENCY_DEFAULT }, 0, 0, VE, "preset" },
    { "llhq",         "low latency hq",                     0,                    AV_OPT_TYPE_CONST, { .i64 = PRESET_LOW_LATENCY_HQ },      0, 0, VE, "preset" },
    { "llhp",         "low latency hp",                     0,                    AV_OPT_TYPE_CONST, { .i64 = PRESET_LOW_LATENCY_HP },      0, 0, VE, "preset" },
    { "lossless",     "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = PRESET_LOSSLESS_DEFAULT },    0, 0, VE, "preset" },
    { "losslesshp",   "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = PRESET_LOSSLESS_HP },         0, 0, VE, "preset" },
#ifdef NVENC_HAVE_NEW_PRESETS
    { "p1",          "fastest (lowest quality)",            0,                    AV_OPT_TYPE_CONST, { .i64 = PRESET_P1 },                  0, 0, VE, "preset" },
    { "p2",          "faster (lower quality)",              0,                    AV_OPT_TYPE_CONST, { .i64 = PRESET_P2 },                  0, 0, VE, "preset" },
    { "p3",          "fast (low quality)",                  0,                    AV_OPT_TYPE_CONST, { .i64 = PRESET_P3 },                  0, 0, VE, "preset" },
    { "p4",          "medium (default)",                    0,                    AV_OPT_TYPE_CONST, { .i64 = PRESET_P4 },                  0, 0, VE, "preset" },
    { "p5",          "slow (good quality)",                 0,                    AV_OPT_TYPE_CONST, { .i64 = PRESET_P5 },                  0, 0, VE, "preset" },
    { "p6",          "slower (better quality)",             0,                    AV_OPT_TYPE_CONST, { .i64 = PRESET_P6 },                  0, 0, VE, "preset" },
    { "p7",          "slowest (best quality)",              0,                    AV_OPT_TYPE_CONST, { .i64 = PRESET_P7 },                  0, 0, VE, "preset" },
    { "tune",        "Set the encoding tuning info",        OFFSET(tuning_info),  AV_OPT_TYPE_INT,   { .i64 = NV_ENC_TUNING_INFO_HIGH_QUALITY }, NV_ENC_TUNING_INFO_HIGH_QUALITY, NV_ENC_TUNING_INFO_LOSSLESS,  VE, "tune" },
    { "hq",          "High quality",                        0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_TUNING_INFO_HIGH_QUALITY },             0, 0, VE, "tune" },
    { "ll",          "Low latency",                         0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_TUNING_INFO_LOW_LATENCY },              0, 0, VE, "tune" },
    { "ull",         "Ultra low latency",                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY },        0, 0, VE, "tune" },
    { "lossless",    "Lossless",                            0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_TUNING_INFO_LOSSLESS },                 0, 0, VE, "tune" },
#endif
    { "profile",      "Set the encoding profile",           OFFSET(profile),      AV_OPT_TYPE_INT,   { .i64 = NV_ENC_H264_PROFILE_MAIN }, NV_ENC_H264_PROFILE_BASELINE, NV_ENC_H264_PROFILE_HIGH_444P, VE, "profile" },
    { "baseline",     "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_H264_PROFILE_BASELINE },  0, 0, VE, "profile" },
    { "main",         "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_H264_PROFILE_MAIN },      0, 0, VE, "profile" },
    { "high",         "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_H264_PROFILE_HIGH },      0, 0, VE, "profile" },
    { "high444p",     "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_H264_PROFILE_HIGH_444P }, 0, 0, VE, "profile" },
#ifdef NVENC_HAVE_H264_LVL6
    { "level",        "Set the encoding level restriction", OFFSET(level),        AV_OPT_TYPE_INT,   { .i64 = NV_ENC_LEVEL_AUTOSELECT }, NV_ENC_LEVEL_AUTOSELECT, NV_ENC_LEVEL_H264_62, VE, "level" },
#else
    { "level",        "Set the encoding level restriction", OFFSET(level),        AV_OPT_TYPE_INT,   { .i64 = NV_ENC_LEVEL_AUTOSELECT }, NV_ENC_LEVEL_AUTOSELECT, NV_ENC_LEVEL_H264_52, VE, "level" },
#endif
    { "auto",         "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_LEVEL_AUTOSELECT },    0, 0, VE, "level" },
    { "1",            "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_LEVEL_H264_1 },        0, 0, VE, "level" },
    { "1.0",          "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_LEVEL_H264_1 },        0, 0, VE, "level" },
    { "1b",           "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_LEVEL_H264_1b },       0, 0, VE, "level" },
    { "1.0b",         "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_LEVEL_H264_1b },       0, 0, VE, "level" },
    { "1.1",          "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_LEVEL_H264_11 },       0, 0, VE, "level" },
    { "1.2",          "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_LEVEL_H264_12 },       0, 0, VE, "level" },
    { "1.3",          "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_LEVEL_H264_13 },       0, 0, VE, "level" },
    { "2",            "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_LEVEL_H264_2 },        0, 0, VE, "level" },
    { "2.0",          "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_LEVEL_H264_2 },        0, 0, VE, "level" },
    { "2.1",          "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_LEVEL_H264_21 },       0, 0, VE, "level" },
    { "2.2",          "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_LEVEL_H264_22 },       0, 0, VE, "level" },
    { "3",            "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_LEVEL_H264_3 },        0, 0, VE, "level" },
    { "3.0",          "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_LEVEL_H264_3 },        0, 0, VE, "level" },
    { "3.1",          "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_LEVEL_H264_31 },       0, 0, VE, "level" },
    { "3.2",          "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_LEVEL_H264_32 },       0, 0, VE, "level" },
    { "4",            "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_LEVEL_H264_4 },        0, 0, VE, "level" },
    { "4.0",          "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_LEVEL_H264_4 },        0, 0, VE, "level" },
    { "4.1",          "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_LEVEL_H264_41 },       0, 0, VE, "level" },
    { "4.2",          "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_LEVEL_H264_42 },       0, 0, VE, "level" },
    { "5",            "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_LEVEL_H264_5 },        0, 0, VE, "level" },
    { "5.0",          "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_LEVEL_H264_5 },        0, 0, VE, "level" },
    { "5.1",          "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_LEVEL_H264_51 },       0, 0, VE, "level" },
    { "5.2",          "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_LEVEL_H264_52 },       0, 0, VE, "level" },
#ifdef NVENC_HAVE_H264_LVL6
    { "6.0",          "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_LEVEL_H264_60 },       0, 0, VE, "level" },
    { "6.1",          "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_LEVEL_H264_61 },       0, 0, VE, "level" },
    { "6.2",          "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_LEVEL_H264_62 },       0, 0, VE, "level" },
#endif
    { "rc",           "Override the preset rate-control",   OFFSET(rc),           AV_OPT_TYPE_INT,   { .i64 = -1 },                                  -1, INT_MAX, VE, "rc" },
    { "constqp",      "Constant QP mode",                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_PARAMS_RC_CONSTQP },                   0, 0, VE, "rc" },
    { "vbr",          "Variable bitrate mode",              0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_PARAMS_RC_VBR },                       0, 0, VE, "rc" },
    { "cbr",          "Constant bitrate mode",              0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_PARAMS_RC_CBR },                       0, 0, VE, "rc" },
    { "vbr_minqp",    "Variable bitrate mode with MinQP (deprecated)", 0,         AV_OPT_TYPE_CONST, { .i64 = RCD(NV_ENC_PARAMS_RC_VBR_MINQP) },            0, 0, VE, "rc" },
    { "ll_2pass_quality", "Multi-pass optimized for image quality (deprecated)",
                                                            0,                    AV_OPT_TYPE_CONST, { .i64 = RCD(NV_ENC_PARAMS_RC_2_PASS_QUALITY) },       0, 0, VE, "rc" },
    { "ll_2pass_size", "Multi-pass optimized for constant frame size (deprecated)",
                                                            0,                    AV_OPT_TYPE_CONST, { .i64 = RCD(NV_ENC_PARAMS_RC_2_PASS_FRAMESIZE_CAP) }, 0, 0, VE, "rc" },
    { "vbr_2pass",    "Multi-pass variable bitrate mode (deprecated)", 0,         AV_OPT_TYPE_CONST, { .i64 = RCD(NV_ENC_PARAMS_RC_2_PASS_VBR) },           0, 0, VE, "rc" },
    { "cbr_ld_hq",    "Constant bitrate low delay high quality mode", 0,          AV_OPT_TYPE_CONST, { .i64 = RCD(NV_ENC_PARAMS_RC_CBR_LOWDELAY_HQ) },      0, 0, VE, "rc" },
    { "cbr_hq",       "Constant bitrate high quality mode", 0,                    AV_OPT_TYPE_CONST, { .i64 = RCD(NV_ENC_PARAMS_RC_CBR_HQ) },               0, 0, VE, "rc" },
    { "vbr_hq",       "Variable bitrate high quality mode", 0,                    AV_OPT_TYPE_CONST, { .i64 = RCD(NV_ENC_PARAMS_RC_VBR_HQ) },               0, 0, VE, "rc" },
    { "rc-lookahead", "Number of frames to look ahead for rate-control",
                                                            OFFSET(rc_lookahead), AV_OPT_TYPE_INT,   { .i64 = 0 }, 0, INT_MAX, VE },
    { "surfaces",     "Number of concurrent surfaces",      OFFSET(nb_surfaces),  AV_OPT_TYPE_INT,   { .i64 = 0 }, 0, MAX_REGISTERED_FRAMES, VE },
    { "cbr",          "Use cbr encoding mode",              OFFSET(cbr),          AV_OPT_TYPE_BOOL,  { .i64 = 0 },   0, 1, VE },
    { "2pass",        "Use 2pass encoding mode",            OFFSET(twopass),      AV_OPT_TYPE_BOOL,  { .i64 = -1 }, -1, 1, VE },
    { "gpu",          "Selects which NVENC capable GPU to use. First GPU is 0, second is 1, and so on.",
                                                            OFFSET(device),       AV_OPT_TYPE_INT,   { .i64 = ANY_DEVICE },   -2, INT_MAX, VE, "gpu" },
    { "any",          "Pick the first device available",    0,                    AV_OPT_TYPE_CONST, { .i64 = ANY_DEVICE },          0, 0, VE, "gpu" },
    { "list",         "List the available devices",         0,                    AV_OPT_TYPE_CONST, { .i64 = LIST_DEVICES },        0, 0, VE, "gpu" },
    { "delay",        "Delay frame output by the given amount of frames",
                                                            OFFSET(async_depth),  AV_OPT_TYPE_INT,   { .i64 = INT_MAX }, 0, INT_MAX, VE },
    { "no-scenecut",  "When lookahead is enabled, set this to 1 to disable adaptive I-frame insertion at scene cuts",
                                                            OFFSET(no_scenecut),  AV_OPT_TYPE_BOOL,  { .i64 = 0 }, 0,  1, VE },
    { "forced-idr",   "If forcing keyframes, force them as IDR frames.",
                                                            OFFSET(forced_idr),   AV_OPT_TYPE_BOOL,  { .i64 = 0 }, -1, 1, VE },
    { "b_adapt",      "When lookahead is enabled, set this to 0 to disable adaptive B-frame decision",
                                                            OFFSET(b_adapt),      AV_OPT_TYPE_BOOL,  { .i64 = 1 }, 0,  1, VE },
    { "spatial-aq",   "set to 1 to enable Spatial AQ",      OFFSET(aq),           AV_OPT_TYPE_BOOL,  { .i64 = 0 }, 0,  1, VE },
    { "spatial_aq",   "set to 1 to enable Spatial AQ",      OFFSET(aq),           AV_OPT_TYPE_BOOL,  { .i64 = 0 }, 0,  1, VE },
    { "temporal-aq",  "set to 1 to enable Temporal AQ",     OFFSET(temporal_aq),  AV_OPT_TYPE_BOOL,  { .i64 = 0 }, 0,  1, VE },
    { "temporal_aq",  "set to 1 to enable Temporal AQ",     OFFSET(temporal_aq),  AV_OPT_TYPE_BOOL,  { .i64 = 0 }, 0,  1, VE },
    { "zerolatency",  "Set 1 to indicate zero latency operation (no reordering delay)",
                                                            OFFSET(zerolatency),  AV_OPT_TYPE_BOOL,  { .i64 = 0 }, 0,  1, VE },
    { "nonref_p",     "Set this to 1 to enable automatic insertion of non-reference P-frames",
                                                            OFFSET(nonref_p),     AV_OPT_TYPE_BOOL,  { .i64 = 0 }, 0,  1, VE },
    { "strict_gop",   "Set 1 to minimize GOP-to-GOP rate fluctuations",
                                                            OFFSET(strict_gop),   AV_OPT_TYPE_BOOL,  { .i64 = 0 }, 0,  1, VE },
    { "aq-strength",  "When Spatial AQ is enabled, this field is used to specify AQ strength. AQ strength scale is from 1 (low) - 15 (aggressive)",
                                                            OFFSET(aq_strength),  AV_OPT_TYPE_INT,   { .i64 = 8 }, 1, 15, VE },
    { "cq",           "Set target quality level (0 to 51, 0 means automatic) for constant quality mode in VBR rate control",
                                                            OFFSET(quality),      AV_OPT_TYPE_FLOAT, { .dbl = 0.}, 0., 51., VE },
    { "aud",          "Use access unit delimiters",         OFFSET(aud),          AV_OPT_TYPE_BOOL,  { .i64 = 0 }, 0, 1, VE },
    { "bluray-compat", "Bluray compatibility workarounds",  OFFSET(bluray_compat),AV_OPT_TYPE_BOOL,  { .i64 = 0 }, 0, 1, VE },
    { "init_qpP",     "Initial QP value for P frame",       OFFSET(init_qp_p),    AV_OPT_TYPE_INT,   { .i64 = -1 }, -1, 51, VE },
    { "init_qpB",     "Initial QP value for B frame",       OFFSET(init_qp_b),    AV_OPT_TYPE_INT,   { .i64 = -1 }, -1, 51, VE },
    { "init_qpI",     "Initial QP value for I frame",       OFFSET(init_qp_i),    AV_OPT_TYPE_INT,   { .i64 = -1 }, -1, 51, VE },
    { "qp",           "Constant quantization parameter rate control method",
                                                            OFFSET(cqp),          AV_OPT_TYPE_INT,   { .i64 = -1 }, -1, 51, VE },
    { "weighted_pred","Set 1 to enable weighted prediction",
                                                            OFFSET(weighted_pred),AV_OPT_TYPE_INT,   { .i64 = 0 }, 0, 1, VE },
    { "coder",        "Coder type",                         OFFSET(coder),        AV_OPT_TYPE_INT,   { .i64 = -1                                         },-1, 2, VE, "coder" },
    { "default",      "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = -1                                         }, 0, 0, VE, "coder" },
    { "auto",         "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_H264_ENTROPY_CODING_MODE_AUTOSELECT }, 0, 0, VE, "coder" },
    { "cabac",        "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_H264_ENTROPY_CODING_MODE_CABAC      }, 0, 0, VE, "coder" },
    { "cavlc",        "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_H264_ENTROPY_CODING_MODE_CAVLC      }, 0, 0, VE, "coder" },
    { "ac",           "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_H264_ENTROPY_CODING_MODE_CABAC      }, 0, 0, VE, "coder" },
    { "vlc",          "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_H264_ENTROPY_CODING_MODE_CAVLC      }, 0, 0, VE, "coder" },
#ifdef NVENC_HAVE_BFRAME_REF_MODE
    { "b_ref_mode",   "Use B frames as references",         OFFSET(b_ref_mode),   AV_OPT_TYPE_INT,   { .i64 = NV_ENC_BFRAME_REF_MODE_DISABLED }, NV_ENC_BFRAME_REF_MODE_DISABLED, NV_ENC_BFRAME_REF_MODE_MIDDLE, VE, "b_ref_mode" },
    { "disabled",     "B frames will not be used for reference", 0,               AV_OPT_TYPE_CONST, { .i64 = NV_ENC_BFRAME_REF_MODE_DISABLED }, 0, 0, VE, "b_ref_mode" },
    { "each",         "Each B frame will be used for reference", 0,               AV_OPT_TYPE_CONST, { .i64 = NV_ENC_BFRAME_REF_MODE_EACH }, 0, 0, VE, "b_ref_mode" },
    { "middle",       "Only (number of B frames)/2 will be used for reference", 0,AV_OPT_TYPE_CONST, { .i64 = NV_ENC_BFRAME_REF_MODE_MIDDLE }, 0, 0, VE, "b_ref_mode" },
#else
    { "b_ref_mode",   "(not supported)",                    OFFSET(b_ref_mode),   AV_OPT_TYPE_INT,   { .i64 = 0 }, 0, INT_MAX, VE, "b_ref_mode" },
    { "disabled",     "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = 0 }, 0, 0,       VE, "b_ref_mode" },
    { "each",         "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = 1 }, 0, 0,       VE, "b_ref_mode" },
    { "middle",       "",                                   0,                    AV_OPT_TYPE_CONST, { .i64 = 2 }, 0, 0,       VE, "b_ref_mode" },
#endif
    { "a53cc",        "Use A53 Closed Captions (if available)", OFFSET(a53_cc),   AV_OPT_TYPE_BOOL,  { .i64 = 1 }, 0, 1, VE },
    { "dpb_size",     "Specifies the DPB size used for encoding (0 means automatic)",
                                                            OFFSET(dpb_size),     AV_OPT_TYPE_INT,   { .i64 = 0 }, 0, INT_MAX, VE },
#ifdef NVENC_HAVE_MULTIPASS
    { "multipass",    "Set the multipass encoding",         OFFSET(multipass),    AV_OPT_TYPE_INT,   { .i64 = NV_ENC_MULTI_PASS_DISABLED },         NV_ENC_MULTI_PASS_DISABLED, NV_ENC_TWO_PASS_FULL_RESOLUTION, VE, "multipass" },
    { "disabled",     "Single Pass",                        0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_MULTI_PASS_DISABLED },         0,                          0,                               VE, "multipass" },
    { "qres",         "Two Pass encoding is enabled where first Pass is quarter resolution",
                                                            0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_TWO_PASS_QUARTER_RESOLUTION }, 0,                          0,                               VE, "multipass" },
    { "fullres",      "Two Pass encoding is enabled where first Pass is full resolution",
                                                            0,                    AV_OPT_TYPE_CONST, { .i64 = NV_ENC_TWO_PASS_FULL_RESOLUTION },    0,                          0,                               VE, "multipass" },
#endif
#ifdef NVENC_HAVE_LDKFS
    { "ldkfs",        "Low delay key frame scale; Specifies the Scene Change frame size increase allowed in case of single frame VBV and CBR",
                                                            OFFSET(ldkfs),        AV_OPT_TYPE_INT,   { .i64 = 0 }, 0, UCHAR_MAX, VE },
#endif
    { NULL }
};
const AVCodecHWConfigInternal *const ff_tfenc_hw_configs[] = {
    HW_CONFIG_ENCODER_FRAMES(CUDA,  CUDA),
    HW_CONFIG_ENCODER_DEVICE(NONE,  CUDA),
#if CONFIG_D3D11VA
    HW_CONFIG_ENCODER_FRAMES(D3D11, D3D11VA),
    HW_CONFIG_ENCODER_DEVICE(NONE,  D3D11VA),
#endif
    NULL,
};
const enum AVPixelFormat ff_tf_enc_pix_fmts[] = {
    AV_PIX_FMT_NV12,
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_P010,
    AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_P016,      // Truncated to 10bits
    AV_PIX_FMT_YUV444P16, // Truncated to 10bits
    AV_PIX_FMT_0RGB32,
    AV_PIX_FMT_0BGR32,
    AV_PIX_FMT_NONE
};

static const AVCodecDefault defaults[] = {
    { "b", "2M" },
    { "qmin", "-1" },
    { "qmax", "-1" },
    { "qdiff", "-1" },
    { "qblur", "-1" },
    { "qcomp", "-1" },
    { "g", "250" },
    { "bf", "-1" },
    { "refs", "0" },
    { NULL },
};

static av_cold int tfenc_setup_surfaces(AVCodecContext *avctx)
{
    TfencContext *ctx = avctx->priv_data;
    int i, res = 0, res2;

    ctx->nb_buffers = 4;

    ctx->output_buffer_ready_queue = av_fifo_alloc(ctx->nb_buffers * sizeof(TfencBuffer*));
    if (!ctx->output_buffer_ready_queue)
        return AVERROR(ENOMEM);

    ctx->timestamp_list = av_fifo_alloc(ctx->nb_buffers * sizeof(int64_t));
    if (!ctx->timestamp_list)
        return AVERROR(ENOMEM);

    return res;
}

static int output_ready(AVCodecContext *avctx, int flush)
{
    TfencContext *ctx = avctx->priv_data;
    int nb_ready;

    nb_ready = av_fifo_size(ctx->output_buffer_ready_queue)   / sizeof(TfencBuffer*);
    return nb_ready > 0;
}
static void ff_tfenc_callback(void *user_param, void *data, int len)
{
    AVCodecContext *avctx = user_param;
    TfencContext *ctx = avctx->priv_data;
    // av_log(avctx, AV_LOG_WARNING, "%s [in]\n", __func__);
    TfencBuffer *buffer = malloc(sizeof(TfencBuffer));
    buffer->data = malloc(len);
    buffer->len = len;

    memcpy(buffer->data,data,len);

    // thread-safe?
    // out of range?
    av_fifo_generic_write(ctx->output_buffer_ready_queue, &buffer, sizeof(buffer), NULL);
}
static av_cold int tfenc_setup_device(AVCodecContext *avctx)
{
    TfencContext *ctx = avctx->priv_data;

    tfenc_callback cb;

    /* encoding setting. */
    tfenc_setting setting;
    setting.pix_format = PIXFMT_NV12;
    setting.width = avctx->width;
    setting.height = avctx->height;
    setting.profile = PROFILE_AVC_MAIN;
    setting.level = 51;
    setting.bit_rate = 4000000 * 4;
    setting.max_bit_rate = 10000000 * 4;
    setting.gop = 50;
    

    if (avctx->framerate.num > 0 && avctx->framerate.den > 0) {
        setting.frame_rate = avctx->framerate.num / avctx->framerate.den;
    } else {
        setting.frame_rate = avctx->time_base.den / (avctx->time_base.num * avctx->ticks_per_frame);
    }
    
    setting.device_id = 0;
    setting.rc_mode   = RC_CBR;



    /* callback parameter. */
    cb.param = avctx;
    cb.func = ff_tfenc_callback;
    // TF_HANDLE handle;
    int ret = 0;

    ret = tfenc_encoder_create(&ctx->handle, &setting, cb);

    if (ret < 0){
        printf("create tfenc failed!\n");
        ctx->handle = NULL;
        return -1;
    }else{
        printf("create tfenc success ,handle %p\n",ctx->handle);
        return 0;
    }
}
static av_cold int ff_tf_enc_init(AVCodecContext *avctx)
{
    av_log(avctx, AV_LOG_WARNING, "%s [in]\n", __func__);
    TfencContext *ctx = avctx->priv_data;
    int ret = 0;
    if ((ret = tfenc_setup_device(avctx)) < 0)
        return ret;

    ctx->frame = av_frame_alloc();
    ctx->inputBuffer = malloc(avctx->width * avctx->height * 3 / 2);
    if (!ctx->frame)
        return AVERROR(ENOMEM);

    if ((ret = tfenc_setup_surfaces(avctx)) < 0)
        return ret;
    
    
    av_log(avctx, AV_LOG_WARNING, "%s [out] \n", __func__);
    return 0;
}
 
static av_cold int ff_tf_enc_close(AVCodecContext *avctx)
{
    av_log(avctx, AV_LOG_WARNING, "%s [in]\n", __func__);

    TfencContext *ctx = avctx->priv_data;

    //TODO check thread safe
    //TODO check leaky output buffer // do1

    TfencBuffer* out_buf;
    while (output_ready(avctx, avctx->internal->draining)) {
        av_fifo_generic_read(ctx->output_buffer_ready_queue, &out_buf, sizeof(out_buf), NULL);

        free(out_buf->data);
        free(out_buf);
    }

    av_fifo_freep(&ctx->timestamp_list);
    av_fifo_freep(&ctx->output_buffer_ready_queue);

    free(ctx->inputBuffer);

    if(ctx->handle)
        tfenc_encoder_destroy(ctx->handle);

    av_log(avctx, AV_LOG_WARNING, "%s [out]\n", __func__);
 
    return 0;
}

static int avframe_get_size(AVCodecContext *avctx, AVFrame *frame) {
    int size = 0;
    int i;

    if (frame->format != AV_PIX_FMT_YUV420P && 
        frame->format != AV_PIX_FMT_NV12){
        av_log(avctx, AV_LOG_ERROR, "%s format is not I420 or nv12\n", __func__);
        return -1;
    }
    for (i = 0; i < AV_NUM_DATA_POINTERS; i++) {
        if (frame->data[i] != NULL) {
            int plane_lines = i == 0 ? frame->height : frame->height / 2;
            size += plane_lines * frame->linesize[i];
        }
    }

    return size;
}
static int copy_avframe_to_buffer(AVFrame *frame, uint8_t *buffer) {
    int y, u, v;
    uint8_t *dst = buffer;
    uint8_t *src_y = frame->data[0];
    uint8_t *src_u = frame->data[1];
    uint8_t *src_v = frame->data[2];
    int width = frame->width;
    int height = frame->height;

    // Copy Y plane
    for (y = 0; y < height; y++) {
        memcpy(dst, src_y, width);
        dst += width;
        src_y += frame->linesize[0];
    }

    if (frame->format == AV_PIX_FMT_YUV420P){
        //420 to NV12
        //TODO libyuv
        for (u = 0; u < height / 2; u++) {
            for (v = 0;v < frame->linesize[1]; v++){
                *dst++ = src_u[v];
                *dst++ = src_v[v];
            }
            src_u += frame->linesize[1];
            src_v += frame->linesize[1];
        }
    }else if (frame->format == AV_PIX_FMT_NV12){

        // Copy UV plane
        for (u = 0; u < height / 2; u++) {
            memcpy(dst, src_u, width);
            dst += width;
            src_u += frame->linesize[1];
        }
    }else{
        printf("%s format is not I420 or nv12\n", __func__);
    }
    return 0;

    // Copy Y plane
    for (y = 0; y < height; y++) {
        memcpy(dst, src_y, width);
        dst += width;
        src_y += frame->linesize[0];
    }

    // Copy U plane
    for (u = 0; u < height / 2; u++) {
        memcpy(dst, src_u, width / 2);
        dst += width / 2;
        src_u += frame->linesize[1];
    }

    // Copy V plane
    for (v = 0; v < height / 2; v++) {
        memcpy(dst, src_v, width / 2);
        dst += width / 2;
        src_v += frame->linesize[2];
    }

    return 0;
}

static inline void timestamp_queue_enqueue(AVFifoBuffer* queue, int64_t timestamp)
{
    av_fifo_generic_write(queue, &timestamp, sizeof(timestamp), NULL);
}

static inline int64_t timestamp_queue_dequeue(AVFifoBuffer* queue)
{
    int64_t timestamp = AV_NOPTS_VALUE;
    if (av_fifo_size(queue) > 0)
        av_fifo_generic_read(queue, &timestamp, sizeof(timestamp), NULL);

    return timestamp;
}
static int tfenc_send_frame(AVCodecContext *avctx, const AVFrame *frame)
{
    TfencContext *ctx = avctx->priv_data;
    int res;
    uint8_t* input;
    // av_log(avctx, AV_LOG_WARNING, "%s [in]\n", __func__);

    if (frame && frame->buf[0]) {
        int frame_alloc_size = avframe_get_size(avctx,frame);

        input = (uint8_t*)ctx->inputBuffer;//malloc(frame_alloc_size);

        copy_avframe_to_buffer(frame,input);

        tfenc_process_frame(ctx->handle,input,frame_alloc_size);

        // printf("[wx]need size %d %d %d %d\n",frame_alloc_size,frame->width,frame->linesize[0],frame->height);

        timestamp_queue_enqueue(ctx->timestamp_list, frame->pts);

        // free(input);
    } else {
    }

    // av_log(avctx, AV_LOG_WARNING, "%s [out]\n", __func__);
    return 0;
}
static int tfenc_set_timestamp(AVCodecContext *avctx,
                               int pts,
                               AVPacket *pkt)
{
    TfencContext *ctx = avctx->priv_data;

    pkt->dts = timestamp_queue_dequeue(ctx->timestamp_list);
    pkt->pts = pkt->dts;//TODO correct tfenc pts, now use dts instead

    int frameIntervalP = 1;//tfenc hardcode
    pkt->dts -= FFMAX(frameIntervalP - 1, 0) * FFMAX(avctx->ticks_per_frame, 1) * FFMAX(avctx->time_base.num, 1);

    return 0;
}

static int process_output_buffer(AVCodecContext *avctx, AVPacket *pkt, TfencBuffer *buf)
{
    TfencContext *ctx = avctx->priv_data;

    int res = 0;

    enum AVPictureType pict_type;

    res = ff_get_encode_buffer(avctx, pkt, buf->len, 0);
    if (res < 0) {
        goto error;
    }
    memcpy(pkt->data, buf->data, buf->len);

    if(analyzeh264Frame(buf->data,buf->len < 200?buf->len:200)) {
        pkt->flags |= AV_PKT_FLAG_KEY;
        pict_type = AV_PICTURE_TYPE_I;
        // printf("[wx] frameI %d\n",buf->len);
    }else{
        pict_type = AV_PICTURE_TYPE_P;
        // printf("[wx] frameP %d\n",buf->len);
    }

#if FF_API_CODED_FRAME
FF_DISABLE_DEPRECATION_WARNINGS
    avctx->coded_frame->pict_type = pict_type;
FF_ENABLE_DEPRECATION_WARNINGS
#endif

    int fakeQP = 51;//TODO correct tfenc qp

    ff_side_data_set_encoder_stats(pkt,
        (fakeQP - 1) * FF_QP2LAMBDA, NULL, 0, pict_type);

    res = tfenc_set_timestamp(avctx, 0, pkt);
    if (res < 0)
        goto error2;
    
    return 0;

error:
    timestamp_queue_dequeue(ctx->timestamp_list);

error2:
    return res;
}
static int ff_tf_enc_receive_packet(AVCodecContext *avctx, AVPacket *pkt)
{
    TfencContext *ctx = avctx->priv_data;
    AVFrame *frame = ctx->frame;
    TfencBuffer *out_buf;
    int res;
    // av_log(avctx, AV_LOG_VERBOSE, "Not implement.\n");

    // av_log(avctx, AV_LOG_WARNING, "%s [in]\n", __func__);

    if (!frame->buf[0]) {
        res = ff_encode_get_frame(avctx, frame);
        if (res < 0 && res != AVERROR_EOF)
            return res;
    }

    res = tfenc_send_frame(avctx, frame);
    if (res < 0) {
        if (res != AVERROR(EAGAIN))
            return res;
    } else
        av_frame_unref(frame);

    if (output_ready(avctx, avctx->internal->draining)) {
        av_fifo_generic_read(ctx->output_buffer_ready_queue, &out_buf, sizeof(out_buf), NULL);

        res = process_output_buffer(avctx, pkt, out_buf);

        if (res)
            return res;

        free(out_buf->data);
        free(out_buf);
    } else if (avctx->internal->draining) {
        return AVERROR_EOF;
    } else {
        return AVERROR(EAGAIN);
    }

    return 0;
}
 

static av_cold void ff_tf_encode_flush(AVCodecContext *avctx)
{
    av_log(avctx, AV_LOG_WARNING, "%s [in]\n", __func__);

    TfencContext *ctx = avctx->priv_data;

    tfenc_send_frame(avctx, NULL);
    av_fifo_reset(ctx->timestamp_list);
    av_log(avctx, AV_LOG_WARNING, "%s [out]\n", __func__);
}
static const AVClass h264_tfenc_class = {
    .class_name = "h264_tfenc",
    .item_name = av_default_item_name,
    .option = options,
    .version = LIBAVUTIL_VERSION_INT,
};
AVCodec ff_h264_tfenc_encoder = {
    .name           = "h264_tfenc",
    .long_name      = NULL_IF_CONFIG_SMALL("TFENC H264 Encoder"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_H264,
    .init           = ff_tf_enc_init,
    .receive_packet = ff_tf_enc_receive_packet,
    .close          = ff_tf_enc_close,
    .flush          = ff_tf_encode_flush,
    .priv_data_size = sizeof(TfencContext),
    .priv_class     = &h264_tfenc_class,
    .defaults       = defaults,
    .capabilities   = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_HARDWARE |
                      AV_CODEC_CAP_ENCODER_FLUSH | AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,    
    .pix_fmts       = ff_tf_enc_pix_fmts,
    .wrapper_name   = "tfenc",
    .hw_configs     = ff_tfenc_hw_configs,
};