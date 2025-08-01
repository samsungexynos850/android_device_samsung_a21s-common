/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "audio_hw_proxy"
#define LOG_NDEBUG 0

//#define VERY_VERY_VERBOSE_LOGGING
#ifdef VERY_VERY_VERBOSE_LOGGING
#define ALOGVV ALOGD
#else
#define ALOGVV(a...) do { } while(0)
#endif

//#define SEAMLESS_DUMP

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <expat.h>

#include <log/log.h>
#include <cutils/str_parms.h>
#include <cutils/properties.h>

#include <audio_utils/channels.h>
#include <audio_utils/primitives.h>
#include <audio_utils/clock.h>
#include <hardware/audio.h>
#include <sound/asound.h>

#include "audio_proxy.h"
#include "audio_proxy_interface.h"
#include "audio_definition.h"
#include "audio_tables.h"
#include "audio_board_info.h"

#ifdef SEC_AUDIO_DUMP
#include "SecCoreUtils_Interface.h"
#endif

#ifdef SEC_AUDIO_RESAMPLER
#include "PostProcessConvertor.h"
#endif

/******************************************************************************/
/**                                                                          **/
/** Audio Proxy is Singleton                                                 **/
/**                                                                          **/
/******************************************************************************/

static struct audio_proxy *instance = NULL;

static struct audio_proxy* getInstance(void)
{
    if (instance == NULL) {
        instance = calloc(1, sizeof(struct audio_proxy));
        ALOGI("proxy-%s: created Audio Proxy Instance!", __func__);
    }
    return instance;
}

static void destroyInstance(void)
{
    if (instance) {
        free(instance);
        instance = NULL;
        ALOGI("proxy-%s: destroyed Audio Proxy Instance!", __func__);
    }
    return;
}

/******************************************************************************/
/**                                                                          **/
/** Utility Interfaces                                                       **/
/**                                                                          **/
/******************************************************************************/
bool is_active_usage_CPCall(void *proxy)
{
    struct audio_proxy *aproxy = (struct audio_proxy *)proxy;

    if (aproxy->active_playback_ausage >= AUSAGE_CPCALL_MIN &&
        aproxy->active_playback_ausage <= AUSAGE_CPCALL_MAX)
        return true;
    else
        return false;
}

bool is_usage_CPCall(audio_usage ausage)
{
    if (ausage >= AUSAGE_CPCALL_MIN && ausage <= AUSAGE_CPCALL_MAX)
        return true;
    else
        return false;
}

bool is_active_usage_APCall(void *proxy)
{
    struct audio_proxy *aproxy = (struct audio_proxy *)proxy;

    if (aproxy->active_playback_ausage >= AUSAGE_APCALL_MIN &&
        aproxy->active_playback_ausage <= AUSAGE_APCALL_MAX)
        return true;
    else
        return false;
}

bool is_usage_APCall(audio_usage ausage)
{
    if (ausage >= AUSAGE_APCALL_MIN && ausage <= AUSAGE_APCALL_MAX)
        return true;
    else
        return false;
}

bool is_usage_Call(audio_usage ausage)
{
    if (ausage >= AUSAGE_CPCALL_MIN && ausage <= AUSAGE_CPCALL_MAX)
        return true;
    else if (ausage >= AUSAGE_APCALL_MIN && ausage <= AUSAGE_APCALL_MAX)
        return true;
    else
        return false;
}

bool is_usage_Loopback(audio_usage ausage)
{
    // AUSAGE_LOOPBACK == min, AUSAGE_LOOPBACK_CODEC == max
    if (ausage >= AUSAGE_LOOPBACK && ausage <= AUSAGE_LOOPBACK_CODEC)
        return true;
    else
        return false;
}

bool is_usb_connected(void)
{
    return false;
}
/******************************************************************************/
/**                                                                          **/
/** Local Fuctions for Audio Device Proxy                                    **/
/**                                                                          **/
/******************************************************************************/

static audio_format_t get_pcmformat_from_alsaformat(enum pcm_format pcmformat)
{
    audio_format_t format = AUDIO_FORMAT_PCM_16_BIT;

    switch (pcmformat) {
        case PCM_FORMAT_S16_LE:
            format = AUDIO_FORMAT_PCM_16_BIT;
            break;
        case PCM_FORMAT_S32_LE:
            format = AUDIO_FORMAT_PCM_32_BIT;
            break;
        case PCM_FORMAT_S8:
            format = AUDIO_FORMAT_PCM_8_BIT;
            break;
        case PCM_FORMAT_S24_LE:
        case PCM_FORMAT_S24_3LE:
            format = AUDIO_FORMAT_PCM_8_24_BIT;
            break;
        case PCM_FORMAT_INVALID:
        case PCM_FORMAT_MAX:
            format = AUDIO_FORMAT_PCM_16_BIT;
            break;
    }

    return format;
}

static bool is_playback_device_bt(device_type device)
{
    if (device == DEVICE_BT_SCO_HEADSET || device == DEVICE_SPEAKER_AND_BT_SCO_HEADSET)
        return true;
    else
        return false;
}

static bool is_playback_device_speaker_dualpath(device_type device)
{
    if (device == DEVICE_SPEAKER_AND_HEADSET
        || device == DEVICE_SPEAKER_AND_HEADPHONE
        || device == DEVICE_SPEAKER_AND_BT_SCO_HEADSET)
        return true;
    else
        return false;
}

static bool is_device_speaker(device_type device)
{
    if (device < DEVICE_MAIN_MIC) {
        if ((device == DEVICE_SPEAKER)
#ifdef SEC_AUDIO_SUPPORT_GAMECHAT_SPK_AEC
                || (device == DEVICE_SPEAKER_GAMING)
#endif
                || (device == DEVICE_SPEAKER_DEX)) {
            return true;
        }
        return false;
    } else {
        if ((device == DEVICE_SPEAKER_MIC)
#ifdef SEC_AUDIO_SUPPORT_GAMECHAT_SPK_AEC
                || (device == DEVICE_SPEAKER_GAMING_MIC)
#endif
                || (device == DEVICE_SPEAKER_DEX_MIC)) {
            return true;
        }
        return false;
    }
}

// If there are specific device number in mixer_paths.xml, it get the specific device number from mixer_paths.xml
static int get_pcm_device_number(void *proxy, void *proxy_stream)
{
    struct audio_proxy *aproxy = proxy;
    struct audio_proxy_stream *apstream = (struct audio_proxy_stream *)proxy_stream;
    int pcm_device_number = -1;
    bool is_direct_link;

    pthread_rwlock_rdlock(&aproxy->mixer_update_lock);
    if (apstream) {
        switch(apstream->stream_type) {
            case ASTREAM_PLAYBACK_PRIMARY:
                pcm_device_number = PRIMARY_PLAYBACK_DEVICE;
                break;

            case ASTREAM_PLAYBACK_FAST:
                pcm_device_number = FAST_PLAYBACK_DEVICE;
                break;

            case ASTREAM_PLAYBACK_LOW_LATENCY:
                pcm_device_number = LOW_PLAYBACK_DEVICE;
                break;

            case ASTREAM_PLAYBACK_DEEP_BUFFER:
                pcm_device_number = ((apstream->pcmconfig.rate > DEFAULT_MEDIA_SAMPLING_RATE) ?
                                     DEEP_PLAYBACK_DIRECT_DEVICE : DEEP_PLAYBACK_DEVICE);
                break;

            case ASTREAM_PLAYBACK_COMPR_OFFLOAD:
                pcm_device_number = OFFLOAD_PLAYBACK_DEVICE;
                break;

            case ASTREAM_PLAYBACK_MMAP:
                pcm_device_number = MMAP_PLAYBACK_DEVICE;
                break;

            case ASTREAM_PLAYBACK_AUX_DIGITAL:
                pcm_device_number = AUX_PLAYBACK_DEVICE;
                break;

            case ASTREAM_CAPTURE_PRIMARY:
                pcm_device_number = PRIMARY_CAPTURE_DEVICE;
                break;

            case ASTREAM_CAPTURE_CALL:
                pcm_device_number = CALL_RECORD_DEVICE;
                break;

            case ASTREAM_CAPTURE_LOW_LATENCY:
                pcm_device_number = LOW_CAPTURE_DEVICE;
                break;

            case ASTREAM_CAPTURE_MMAP:
                pcm_device_number = MMAP_CAPTURE_DEVICE;
                break;

            case ASTREAM_CAPTURE_FM_TUNER:
            case ASTREAM_CAPTURE_FM_RECORDING:
                pcm_device_number = FM_RECORD_DEVICE;
                break;

            default:
                break;
        }
    } else {
    }
    pthread_rwlock_unlock(&aproxy->mixer_update_lock);

    return pcm_device_number;
}

/*
 * Internal Path Control Functions for A-Box
 */
static void disable_voice_tx_direct_in(void *proxy)
{
    struct audio_proxy *aproxy = proxy;
    char pcm_path[MAX_PCM_PATH_LEN];

    if (aproxy->call_tx_direct) {
        snprintf(pcm_path, sizeof(pcm_path), "/dev/snd/pcmC%uD%u%c",
                 VC_FMRADIO_CAPTURE_CARD, VC_FMRADIO_CAPTURE_DEVICE, 'c');

        pcm_stop(aproxy->call_tx_direct);
        pcm_close(aproxy->call_tx_direct);
        aproxy->call_tx_direct= NULL;
        ALOGI("proxy-%s: Voice Call TX Direct PCM Device(%s) is stopped & closed!", __func__, pcm_path);
    }

    return;
}

static void enable_voice_tx_direct_in(void *proxy, device_type target_device __unused)
{
    struct audio_proxy *aproxy = proxy;
    struct pcm_config pcmconfig;
    char pcm_path[MAX_PCM_PATH_LEN];

    if (aproxy->call_tx_direct== NULL) {
        pcmconfig = pcm_config_vc_fmradio_capture;

        snprintf(pcm_path, sizeof(pcm_path), "/dev/snd/pcmC%uD%u%c",
                 VC_FMRADIO_CAPTURE_CARD, VC_FMRADIO_CAPTURE_DEVICE, 'c');

        aproxy->call_tx_direct = pcm_open(VC_FMRADIO_CAPTURE_CARD,
                                        VC_FMRADIO_CAPTURE_DEVICE,
                                        PCM_IN | PCM_MONOTONIC, &pcmconfig);
        if (aproxy->call_tx_direct && !pcm_is_ready(aproxy->call_tx_direct)) {
            /* pcm_open does always return pcm structure, not NULL */
            ALOGE("proxy-%s: Voice Call TX Direct PCM Device(%s) with SR(%u) PF(%d) CC(%d) is not ready as error(%s)",
                  __func__, pcm_path, pcmconfig.rate, pcmconfig.format, pcmconfig.channels,
                  pcm_get_error(aproxy->call_tx_direct));
            goto err_open;
        }
        ALOGVV("proxy-%s: Voice Call TX Direct PCM Device(%s) with SR(%u) PF(%d) CC(%d) is opened",
              __func__, pcm_path, pcmconfig.rate, pcmconfig.format, pcmconfig.channels);

        if (pcm_start(aproxy->call_tx_direct) == 0) {
            ALOGI("proxy-%s: Voice Call TX Direct PCM Device(%s) with SR(%u) PF(%d) CC(%d) is opened & started",
                  __func__, pcm_path, pcmconfig.rate, pcmconfig.format, pcmconfig.channels);
        } else {
            ALOGE("proxy-%s: Voice Call TX Direct PCM Device(%s) with SR(%u) PF(%d) CC(%d) cannot be started as error(%s)",
                  __func__, pcm_path, pcmconfig.rate, pcmconfig.format, pcmconfig.channels,
                  pcm_get_error(aproxy->call_tx_direct));
            goto err_open;
        }
    }

    return;
err_open:
    disable_voice_tx_direct_in(proxy);
}

// Specific Mixer Control Functions for Internal Loopback Handling
void proxy_set_mixercontrol(struct audio_proxy *aproxy, erap_trigger type, int value)
{
    struct mixer_ctl *ctrl = NULL;
    char mixer_name[MAX_MIXER_NAME_LEN];
    int ret = 0, val = value;

    pthread_rwlock_rdlock(&aproxy->mixer_update_lock);

    if (type == MUTE_CONTROL) {
        ctrl = mixer_get_ctl_by_name(aproxy->mixer, ABOX_MUTE_CONTROL_NAME);
        snprintf(mixer_name, sizeof(mixer_name), ABOX_MUTE_CONTROL_NAME);
    } else if (type == TICKLE_CONTROL) {
        ctrl = mixer_get_ctl_by_name(aproxy->mixer, ABOX_TICKLE_CONTROL_NAME);
        snprintf(mixer_name, sizeof(mixer_name), ABOX_TICKLE_CONTROL_NAME);
    }

    if (ctrl) {
        ret = mixer_ctl_set_value(ctrl, 0,val);
        if (ret != 0)
            ALOGE("proxy-%s: failed to set Mixer Control(%s)", __func__, mixer_name);
        else
            ALOGI("proxy-%s: set Mixer Control(%s) to %d", __func__, mixer_name, val);
    } else {
        ALOGE("proxy-%s: cannot find Mixer Control", __func__);
    }

    pthread_rwlock_unlock(&aproxy->mixer_update_lock);

    return ;
}

/* Prepare loopback node pcm configs before actual routing is started
 * to sync automatic loopback node opening sequence and pcm configs used
 */
static void prepare_routing_device_config(void *proxy, int ausage, device_type target_device)
{
    return;
}

static void enable_internal_path(void *proxy, int ausage, device_type target_device)
{
    return;
}

static void disable_internal_path(void *proxy, int ausage, device_type target_device)
{
    return;
}

// Voice Call PCM Handler
static void voice_rx_stop(struct audio_proxy *aproxy)
{
    char pcm_path[MAX_PCM_PATH_LEN];

    /* Disables Voice Call RX Playback Stream */
    if (aproxy->call_rx) {
        snprintf(pcm_path, sizeof(pcm_path), "/dev/snd/pcmC%uD%u%c",
                 VRX_PLAYBACK_CARD, VRX_PLAYBACK_DEVICE, 'p');

        pcm_stop(aproxy->call_rx);
        pcm_close(aproxy->call_rx);
        aproxy->call_rx = NULL;

        ALOGI("proxy-%s: Voice Call RX PCM Device(%s) is stopped & closed!", __func__, pcm_path);
    }
}

static int voice_rx_start(struct audio_proxy *aproxy)
{
    struct pcm_config pcmconfig = pcm_config_voicerx_playback;
    char pcm_path[MAX_PCM_PATH_LEN];

    /* Enables Voice Call RX Playback Stream */
    if (aproxy->call_rx == NULL) {
        snprintf(pcm_path, sizeof(pcm_path), "/dev/snd/pcmC%uD%u%c",
                 VRX_PLAYBACK_CARD, VRX_PLAYBACK_DEVICE, 'p');

        aproxy->call_rx = pcm_open(VRX_PLAYBACK_CARD, VRX_PLAYBACK_DEVICE,
                                   PCM_OUT | PCM_MONOTONIC, &pcmconfig);
        if (aproxy->call_rx && !pcm_is_ready(aproxy->call_rx)) {
            /* pcm_open does always return pcm structure, not NULL */
            ALOGE("proxy-%s: Voice Call RX PCM Device(%s) with SR(%u) PF(%d) CC(%d) is not ready as error(%s)",
                  __func__, pcm_path, pcmconfig.rate, pcmconfig.format, pcmconfig.channels,
                  pcm_get_error(aproxy->call_rx));
            goto err_open;
        }
        ALOGVV("proxy-%s: Voice Call RX PCM Device(%s) with SR(%u) PF(%d) CC(%d) is opened",
              __func__, pcm_path, pcmconfig.rate, pcmconfig.format, pcmconfig.channels);

        if (pcm_start(aproxy->call_rx) == 0) {
            ALOGI("proxy-%s: Voice Call RX PCM Device(%s) with SR(%u) PF(%d) CC(%d) is opened & started",
                  __func__, pcm_path, pcmconfig.rate, pcmconfig.format, pcmconfig.channels);
        } else {
            ALOGE("proxy-%s: Voice Call RX PCM Device(%s) with SR(%u) PF(%d) CC(%d) cannot be started as error(%s)",
                  __func__, pcm_path, pcmconfig.rate, pcmconfig.format, pcmconfig.channels,
                  pcm_get_error(aproxy->call_rx));
            goto err_open;
        }
    }
    return 0;

err_open:
    voice_rx_stop(aproxy);
    return -1;
}

static void voice_tx_stop(struct audio_proxy *aproxy)
{
    char pcm_path[MAX_PCM_PATH_LEN];

    /* Disables Voice Call TX Capture Stream */
    if (aproxy->call_tx) {
        snprintf(pcm_path, sizeof(pcm_path), "/dev/snd/pcmC%uD%u%c",
                 VTX_CAPTURE_CARD, VTX_CAPTURE_DEVICE, 'c');

        pcm_stop(aproxy->call_tx);
        pcm_close(aproxy->call_tx);
        aproxy->call_tx = NULL;
        ALOGI("proxy-%s: Voice Call TX PCM Device(%s) is stopped & closed!", __func__, pcm_path);
    }
}

static int voice_tx_start(struct audio_proxy *aproxy)
{
    struct pcm_config pcmconfig;
    char pcm_path[MAX_PCM_PATH_LEN];

    /* Enables Voice Call TX Capture Stream */
    if (aproxy->call_tx == NULL) {
        pcmconfig = pcm_config_voicetx_capture;

        snprintf(pcm_path, sizeof(pcm_path), "/dev/snd/pcmC%uD%u%c",
                 VTX_CAPTURE_CARD, VTX_CAPTURE_DEVICE, 'c');

        aproxy->call_tx = pcm_open(VTX_CAPTURE_CARD, VTX_CAPTURE_DEVICE,
                                   PCM_IN | PCM_MONOTONIC, &pcmconfig);
        if (aproxy->call_tx && !pcm_is_ready(aproxy->call_tx)) {
            /* pcm_open does always return pcm structure, not NULL */
            ALOGE("proxy-%s: Voice Call TX PCM Device(%s) with SR(%u) PF(%d) CC(%d) is not ready as error(%s)",
                  __func__, pcm_path, pcmconfig.rate, pcmconfig.format, pcmconfig.channels,
                  pcm_get_error(aproxy->call_tx));
            goto err_open;
        }
        ALOGVV("proxy-%s: Voice Call TX PCM Device(%s) with SR(%u) PF(%d) CC(%d) is opened",
              __func__, pcm_path, pcmconfig.rate, pcmconfig.format, pcmconfig.channels);

        if (pcm_start(aproxy->call_tx) == 0) {
            ALOGI("proxy-%s: Voice Call TX PCM Device(%s) with SR(%u) PF(%d) CC(%d) is opened & started",
                  __func__, pcm_path, pcmconfig.rate, pcmconfig.format, pcmconfig.channels);
        } else {
            ALOGE("proxy-%s: Voice Call TX PCM Device(%s) with SR(%u) PF(%d) CC(%d) cannot be started as error(%s)",
                  __func__, pcm_path, pcmconfig.rate, pcmconfig.format, pcmconfig.channels,
                  pcm_get_error(aproxy->call_tx));
            goto err_open;
        }
    }
    return 0;

err_open:
    voice_tx_stop(aproxy);
    return -1;
}

// FM Radio PCM Handler
static void fmradio_playback_stop(struct audio_proxy *aproxy)
{
    char pcm_path[MAX_PCM_PATH_LEN];

    /* Disables FM Radio Playback Stream */
    if (aproxy->fm_playback) {
        snprintf(pcm_path, sizeof(pcm_path), "/dev/snd/pcmC%uD%u%c",
                 FMRADIO_PLAYBACK_CARD, FMRADIO_PLAYBACK_DEVICE, 'p');

        pcm_stop(aproxy->fm_playback);
        pcm_close(aproxy->fm_playback);
        aproxy->fm_playback = NULL;

        ALOGI("proxy-%s: FM Radio Playback PCM Device(%s) is stopped & closed!", __func__, pcm_path);
    }
}

static int fmradio_playback_start(struct audio_proxy *aproxy)
{
    struct pcm_config pcmconfig = pcm_config_fmradio_playback;
    char pcm_path[MAX_PCM_PATH_LEN];

    /* Enables RM Radio Playback Stream */
    if (aproxy->fm_playback == NULL) {
        snprintf(pcm_path, sizeof(pcm_path), "/dev/snd/pcmC%uD%u%c",
                 FMRADIO_PLAYBACK_CARD, FMRADIO_PLAYBACK_DEVICE, 'p');

        aproxy->fm_playback = pcm_open(FMRADIO_PLAYBACK_CARD, FMRADIO_PLAYBACK_DEVICE,
                                       PCM_OUT | PCM_MONOTONIC, &pcmconfig);
        if (aproxy->fm_playback && !pcm_is_ready(aproxy->fm_playback)) {
            /* pcm_open does always return pcm structure, not NULL */
            ALOGE("proxy-%s: FM Radio Playback PCM Device(%s) with SR(%u) PF(%d) CC(%d) is not ready as error(%s)",
                  __func__, pcm_path, pcmconfig.rate, pcmconfig.format, pcmconfig.channels,
                  pcm_get_error(aproxy->fm_playback));
            goto err_open;
        }
        ALOGVV("proxy-%s: FM Radio Playback PCM Device(%s) with SR(%u) PF(%d) CC(%d) is opened",
              __func__, pcm_path, pcmconfig.rate, pcmconfig.format, pcmconfig.channels);

        if (pcm_start(aproxy->fm_playback) == 0) {
            ALOGI("proxy-%s: FM Radio Playback PCM Device(%s) with SR(%u) PF(%d) CC(%d) is opened & started",
                  __func__, pcm_path, pcmconfig.rate, pcmconfig.format, pcmconfig.channels);
        } else {
            ALOGE("proxy-%s: FM Radio Playback PCM Device(%s) with SR(%u) PF(%d) CC(%d) cannot be started as error(%s)",
                  __func__, pcm_path, pcmconfig.rate, pcmconfig.format, pcmconfig.channels,
                  pcm_get_error(aproxy->fm_playback));
            goto err_open;
        }
    }

    return 0;

err_open:
    fmradio_playback_stop(aproxy);
    return -1;
}

static void fmradio_capture_stop(struct audio_proxy *aproxy)
{
    char pcm_path[MAX_PCM_PATH_LEN];

    /* Disables FM Radio Capture Stream */
    if (aproxy->fm_capture) {
        snprintf(pcm_path, sizeof(pcm_path), "/dev/snd/pcmC%uD%u%c",
                 VC_FMRADIO_CAPTURE_CARD, FMRADIO_TRIGGER_DEVICE, 'c');

        pcm_stop(aproxy->fm_capture);
        pcm_close(aproxy->fm_capture);
        aproxy->fm_capture = NULL;

        ALOGI("proxy-%s: FM Radio Capture PCM Device(%s) is stopped & closed!", __func__, pcm_path);
    }
}

static int fmradio_capture_start(struct audio_proxy *aproxy)
{
    struct pcm_config pcmconfig = pcm_config_vc_fmradio_capture;
    char pcm_path[MAX_PCM_PATH_LEN];

    /* Enables RM Radio Capture Stream */
    if (aproxy->fm_capture == NULL) {
        snprintf(pcm_path, sizeof(pcm_path), "/dev/snd/pcmC%uD%u%c",
                 VC_FMRADIO_CAPTURE_CARD, FMRADIO_TRIGGER_DEVICE, 'c');

#ifdef SEC_AUDIO_SUPPORT_LISTENBACK_DSPEFFECT
        else if (aproxy->active_capture_ausage == AUSAGE_LISTENBACK &&
                    aproxy->active_capture_device == DEVICE_MAIN_MIC) {
            /* Listen back Main-Mic case channels set to 8 to match seriallif channels */
            pcmconfig.channels = MEDIA_8_CHANNELS; // required to match seriallif channels
        }
#endif
        aproxy->fm_capture = pcm_open(VC_FMRADIO_CAPTURE_CARD, FMRADIO_TRIGGER_DEVICE,
                                                           PCM_IN | PCM_MONOTONIC, &pcmconfig);
        if (aproxy->fm_capture && !pcm_is_ready(aproxy->fm_capture)) {
            /* pcm_open does always return pcm structure, not NULL */
            ALOGE("proxy-%s: FM Radio Capture PCM Device(%s) with SR(%u) PF(%d) CC(%d) is not ready as error(%s)",
                  __func__, pcm_path, pcmconfig.rate, pcmconfig.format, pcmconfig.channels,
                  pcm_get_error(aproxy->fm_capture));
            goto err_open;
        }
        ALOGVV("proxy-%s: FM Radio Capture PCM Device(%s) with SR(%u) PF(%d) CC(%d) is opened",
              __func__, pcm_path, pcmconfig.rate, pcmconfig.format, pcmconfig.channels);

        if (pcm_start(aproxy->fm_capture) == 0) {
            ALOGI("proxy-%s: FM Radio Capture PCM Device(%s) with SR(%u) PF(%d) CC(%d) is opened & started",
                  __func__, pcm_path, pcmconfig.rate, pcmconfig.format, pcmconfig.channels);
        } else {
            ALOGE("proxy-%s: FM Radio Capture PCM Device(%s) with SR(%u) PF(%d) CC(%d) cannot be started as error(%s)",
                  __func__, pcm_path, pcmconfig.rate, pcmconfig.format, pcmconfig.channels,
                  pcm_get_error(aproxy->fm_capture));
            goto err_open;
        }
    }

    return 0;

err_open:
    fmradio_capture_stop(aproxy);
    return -1;
}

struct mixer {
    int fd;
    struct snd_ctl_card_info card_info;
    struct snd_ctl_elem_info *elem_info;
    struct mixer_ctl *ctl;
    unsigned int count;
};

static struct snd_ctl_event *mixer_read_event_sec(struct mixer *mixer, unsigned int mask)
{
    struct snd_ctl_event *ev;

    if (!mixer)
        return 0;

    ev = calloc(1, sizeof(*ev));
    if (!ev)
        return 0;

    while (read(mixer->fd, ev, sizeof(*ev)) > 0) {
        if (ev->type != SNDRV_CTL_EVENT_ELEM)
            continue;

        if (!(ev->data.elem.mask & mask))
            continue;

        return ev;
    }

    free(ev);
    return 0;
}

static int audio_route_missing_ctl(struct audio_route *ar) {
    return 0;
}

/* Mask for mixer_read_event()
 * It should be same with SNDRV_CTL_EVENT_MASK_* in asound.h.
 */
#define MIXER_EVENT_VALUE    (1 << 0)
#define MIXER_EVENT_INFO     (1 << 1)
#define MIXER_EVENT_ADD      (1 << 2)
#define MIXER_EVENT_TLV      (1 << 3)
#define MIXER_EVENT_REMOVE   (~0U)

static void *mixer_update_loop(void *context)
{
    struct audio_proxy *aproxy = (struct audio_proxy *)context;
    struct snd_ctl_event *event = NULL;
    struct timespec ts_start, ts_tick;

    ALOGI("proxy-%s: started running Mixer Updater Thread", __func__);

    clock_gettime(CLOCK_MONOTONIC, &ts_start);
    do {
        if (aproxy->mixer) {
            ALOGD("proxy-%s: wait add event", __func__);
            event = mixer_read_event_sec(aproxy->mixer, MIXER_EVENT_ADD);
            if (!event) {
                ALOGE("proxy-%s: returned as error or mixer close", __func__);
                clock_gettime(CLOCK_MONOTONIC, &ts_tick);
                if ((ts_tick.tv_sec - ts_start.tv_sec) > MIXER_UPDATE_TIMEOUT) {
                    ALOGI("proxy-%s: Mixer Update Timeout, it will be destroyed", __func__);
                    break;
                }
                continue;
            }
            ALOGD("proxy-%s: returned as add event", __func__);
        } else
            continue;

        pthread_rwlock_wrlock(&aproxy->mixer_update_lock);

        mixer_close(aproxy->mixer);
        aproxy->mixer = mixer_open(MIXER_CARD0);
        if (!aproxy->mixer)
            ALOGE("proxy-%s: failed to re-open Mixer", __func__);

        mixer_subscribe_events(aproxy->mixer, 1);
        audio_route_free(aproxy->aroute);
        aproxy->aroute = audio_route_init(MIXER_CARD0, aproxy->xml_path);
        if (!aproxy->aroute)
            ALOGE("proxy-%s: failed to re-init audio route", __func__);

        ALOGI("proxy-%s: mixer and route are updated", __func__);

        pthread_rwlock_unlock(&aproxy->mixer_update_lock);
        free(event);
    } while (aproxy->mixer && aproxy->aroute && audio_route_missing_ctl(aproxy->aroute));

    ALOGI("proxy-%s: all mixer controls are found", __func__);

    if (aproxy->mixer)
        mixer_subscribe_events(aproxy->mixer, 0);

    ALOGI("proxy-%s: stopped running Mixer Updater Thread", __func__);
    return NULL;
}

static void make_path(audio_usage ausage, device_type device, char *path_name)
{
    memset(path_name, 0, MAX_PATH_NAME_LEN);
    strlcpy(path_name, usage_path_table[ausage], MAX_PATH_NAME_LEN);
    if (strlen(device_table[device]) > 0) {
        strlcat(path_name, "-", MAX_PATH_NAME_LEN);
        strlcat(path_name, device_table[device], MAX_PATH_NAME_LEN);
    }

    return ;
}

static void make_gain(char *path_name, char *gain_name)
{
    memset(gain_name, 0, MAX_GAIN_PATH_NAME_LEN);
    strlcpy(gain_name, "gain-", MAX_PATH_NAME_LEN);
    strlcat(gain_name, path_name, MAX_PATH_NAME_LEN);

    return ;
}

static void add_dual_path(void *proxy, char *path_name)
{
    struct audio_proxy *aproxy = proxy;

    if (aproxy->support_dualspk) {
        char tempStr[MAX_PATH_NAME_LEN] = {0};
        char* szDump;
        szDump = strstr(path_name, "speaker");

        // do not add dual- path for loopback
        if (strstr(path_name, "loopback")) {
            return ;
        }

        if (szDump != NULL) {
            char tempRet[MAX_PATH_NAME_LEN] = {0};
            strncpy(tempStr, path_name, szDump - path_name);
            sprintf(tempRet, "%s%s%s", tempStr, "dual-", szDump);
            strncpy(path_name, tempRet, MAX_PATH_NAME_LEN);
        }
    }
}

/* Enable new Modifier */
static void set_modifier(void *proxy, modifier_type modifier)
{
    struct audio_proxy *aproxy = proxy;

    pthread_rwlock_rdlock(&aproxy->mixer_update_lock);

    audio_route_apply_and_update_path(aproxy->aroute, modifier_table[modifier]);
    ALOGI("proxy-%s: enabled to %s", __func__, modifier_table[modifier]);

    pthread_rwlock_unlock(&aproxy->mixer_update_lock);

    return ;
}

/* Update Modifier */
static void update_modifier(void *proxy, modifier_type old_modifier, modifier_type new_modifier)
{
    struct audio_proxy *aproxy = proxy;

    pthread_rwlock_rdlock(&aproxy->mixer_update_lock);

    // 1. Unset Active Modifier
    audio_route_reset_path(aproxy->aroute, modifier_table[old_modifier]);
    ALOGI("proxy-%s: disabled %s", __func__, modifier_table[old_modifier]);

    // 2. Set New Modifier
    audio_route_apply_path(aproxy->aroute, modifier_table[new_modifier]);
    ALOGI("proxy-%s: enabled %s", __func__, modifier_table[new_modifier]);

    // 3. Update Mixers
    audio_route_update_mixer(aproxy->aroute);

    pthread_rwlock_unlock(&aproxy->mixer_update_lock);

    return ;
}

/* Disable Modifier */
static void reset_modifier(void *proxy, modifier_type modifier)
{
    struct audio_proxy *aproxy = proxy;

    /* Skip resetting BT UAIF rate for Tx if BT playback A2DP or SCO is active */
    if (is_playback_device_bt(aproxy->active_playback_device) &&
        (modifier == MODIFIER_BT_SCO_TX_NB || modifier == MODIFIER_BT_SCO_TX_WB)) {
        ALOGI("proxy-%s: BT playback active skip disabling %s", __func__, modifier_table[modifier]);
        return;
    }

    pthread_rwlock_rdlock(&aproxy->mixer_update_lock);

    audio_route_reset_and_update_path(aproxy->aroute, modifier_table[modifier]);
    ALOGI("proxy-%s: disabled %s", __func__, modifier_table[modifier]);

    pthread_rwlock_unlock(&aproxy->mixer_update_lock);

    return ;
}

/* Enable new Audio Path */
static void set_route(void *proxy, audio_usage ausage, device_type device, int modifier)
{
    struct audio_proxy *aproxy = proxy;
    char path_name[MAX_PATH_NAME_LEN];
    char gain_name[MAX_GAIN_PATH_NAME_LEN];

    modifier_type routed_modifier = (modifier_type)modifier;
    modifier_type *cur_modifier = ((device < DEVICE_MAIN_MIC) ?
                                    &aproxy->active_playback_modifier :
                                    &aproxy->active_capture_modifier); //as reroute can be playback or capture

    if (device == DEVICE_AUX_DIGITAL)
        return ;

    pthread_rwlock_rdlock(&aproxy->mixer_update_lock);

    /* Audio Path Modifier for playback or capture Path,
     * should be reset after previous path is unrouted and new modifier set before actual routing,
     * as loopback nodes are automatically controlled by kernel while routing
     */
    if (routed_modifier >= MODIFIER_BT_SCO_RX_NB && routed_modifier < MODIFIER_NONE) {
        if (*cur_modifier == MODIFIER_NONE)
            set_modifier(aproxy, routed_modifier);
        else
            update_modifier(aproxy, *cur_modifier, routed_modifier);
    } else if (routed_modifier == MODIFIER_NONE && *cur_modifier != MODIFIER_NONE)
        reset_modifier(aproxy, *cur_modifier);

    *cur_modifier = routed_modifier;

    make_path(ausage, device, path_name);
    add_dual_path(aproxy, path_name);
    audio_route_apply_and_update_path(aproxy->aroute, path_name);
    ALOGI("proxy-%s: routed to %s", __func__, path_name);

    make_gain(path_name, gain_name);
    audio_route_apply_and_update_path(aproxy->aroute, gain_name);
    ALOGI("proxy-%s: set gain as %s", __func__, gain_name);

    pthread_rwlock_unlock(&aproxy->mixer_update_lock);

    return ;
}

/* reroute Audio Path */
static void set_reroute(void *proxy, audio_usage old_ausage, device_type old_device,
                                     audio_usage new_ausage, device_type new_device, int modifier)
{
    struct audio_proxy *aproxy = proxy;
    char path_name[MAX_PATH_NAME_LEN];
    char gain_name[MAX_GAIN_PATH_NAME_LEN];

    ALOGI("proxy-%s: enter", __func__);
    modifier_type routed_modifier = (modifier_type)modifier;
    modifier_type *cur_modifier = ((new_device < DEVICE_MAIN_MIC) ?
                                    &aproxy->active_playback_modifier :
                                    &aproxy->active_capture_modifier); //as reroute can be playback or capture

    pthread_rwlock_rdlock(&aproxy->mixer_update_lock);

    // 1. Unset Active Route
    make_path(old_ausage, old_device, path_name);
    add_dual_path(aproxy, path_name);
    /* Updated to reset_and_update to match Q audio-route changes
     * otherwise noise issue happened in alarm/ringtone scenarios
     */
    audio_route_reset_and_update_path(aproxy->aroute, path_name);
    ALOGI("proxy-%s: unrouted %s", __func__, path_name);

    make_gain(path_name, gain_name);
    audio_route_reset_and_update_path(aproxy->aroute, gain_name);
    ALOGI("proxy-%s: reset gain %s", __func__, gain_name);

    if ((strstr(path_name, "speaker") != NULL) && new_device == DEVICE_USB_HEADSET)
        ALOGV("%s: *USB_9 func_disable_spk_enable_usb", __func__);

    /* Audio Path Modifier for playback or capture Path,
     * should be reset after previous path is unrouted and new modifier set before actual routing,
     * as loopback nodes are automatically controlled by kernel while routing
     */
    if (routed_modifier >= MODIFIER_BT_SCO_RX_NB && routed_modifier < MODIFIER_NONE) {
        if (*cur_modifier == MODIFIER_NONE)
            set_modifier(aproxy, routed_modifier);
        else
            update_modifier(aproxy, *cur_modifier, routed_modifier);
    } else if (routed_modifier == MODIFIER_NONE && *cur_modifier != MODIFIER_NONE)
        reset_modifier(aproxy, *cur_modifier);

    *cur_modifier = routed_modifier;

    // 2. Set New Route
    if (new_device != DEVICE_AUX_DIGITAL) {
        make_path(new_ausage, new_device, path_name);
        add_dual_path(aproxy, path_name);
        audio_route_apply_and_update_path(aproxy->aroute, path_name);
        ALOGI("proxy-%s: routed %s", __func__, path_name);

        make_gain(path_name, gain_name);
        audio_route_apply_and_update_path(aproxy->aroute, gain_name);
        ALOGI("proxy-%s: set gain as %s", __func__, gain_name);
    }

    // 3. Update Mixers
    audio_route_update_mixer(aproxy->aroute);

    pthread_rwlock_unlock(&aproxy->mixer_update_lock);

    return ;
}

/* Disable Audio Path */
static void reset_route(void *proxy, audio_usage ausage, device_type device)
{
    struct audio_proxy *aproxy = proxy;
    char path_name[MAX_PATH_NAME_LEN];
    char gain_name[MAX_GAIN_PATH_NAME_LEN];

    pthread_rwlock_rdlock(&aproxy->mixer_update_lock);

    make_path(ausage, device, path_name);
    add_dual_path(aproxy, path_name);
    audio_route_reset_and_update_path(aproxy->aroute, path_name);
    ALOGI("proxy-%s: unrouted %s", __func__, path_name);

    make_gain(path_name, gain_name);
    audio_route_reset_and_update_path(aproxy->aroute, gain_name);
    ALOGI("proxy-%s: reset gain %s", __func__, gain_name);

    pthread_rwlock_unlock(&aproxy->mixer_update_lock);

    return ;
}

static void do_operations_by_playback_route_set(struct audio_proxy *aproxy,
                                                audio_usage routed_ausage, device_type routed_device)
{
    /* skip internal pcm controls */
    if (aproxy->skip_internalpath) {
        ALOGI("proxy-%s: skip internal path pcm controls", __func__);
        return;
    }

    /* Open/Close FM Radio PCM node based on Enable/disable */
    if (routed_ausage != AUSAGE_FM_RADIO_TUNER &&
        routed_ausage != AUSAGE_FM_RADIO_CAPTURE) {
        fmradio_playback_stop(aproxy);
        fmradio_capture_stop(aproxy);
    }

    /* Set Mute during APCall Path Change */
    if ((aproxy->active_playback_device != routed_device) &&
        (is_active_usage_APCall(aproxy) || is_usage_APCall(routed_ausage)))
        proxy_set_mixercontrol(aproxy, MUTE_CONTROL, ABOX_MUTE_CNT_FOR_PATH_CHANGE);

    return ;
}

static void do_operations_by_playback_route_reset(struct audio_proxy *aproxy __unused)
{
    return ;
}

static void clr_call_path_param(void)
{
    struct audio_proxy *aproxy = getInstance();

    ALOGI("proxy-%s: enter", __func__);

    aproxy->call_param_idx.special = 0;
    aproxy->call_param_idx.reserved = 0;
    aproxy->call_param_idx.channel = 0;
    aproxy->call_param_idx.mic_num = 0;
    aproxy->call_param_idx.tty_mode = 0;
    aproxy->call_param_idx.call_type = 0;
    aproxy->call_param_idx.rate_band = 0;
    aproxy->call_param_idx.device = 0;
}

static void set_call_path_param(void)
{
    struct audio_proxy *aproxy = getInstance();
    uint32_t *call_path_param = NULL;

    // can't get these params, for now
    aproxy->call_param_idx.special = 0;
    aproxy->call_param_idx.reserved = 0;
    aproxy->call_param_idx.channel = 0;
    aproxy->call_param_idx.mic_num = 0;

    call_path_param = (uint32_t *)&aproxy->call_param_idx;

    /* Clear All call path parameter.
     * ABOX firmware doesn't use the parameter in GED */
    clr_call_path_param();

    ALOGI("proxy-%s: Call Path Parameter IDX 0x%x", __func__, *call_path_param);

    proxy_set_mixer_value_int(aproxy, ABOX_CALL_PATH_PARAM, *call_path_param);
}

/*
 * Dump functions
 */
static void calliope_cleanup_old(const char *path, const char *prefix)
{
    struct dirent **namelist;
    int n, match = 0;

    ALOGV("proxy-%s", __func__);

    n = scandir(path, &namelist, NULL, alphasort);
    if (n > 0) {
        /* interate in reverse order to get old file */
        while (n--) {
            if (strstr(namelist[n]->d_name, prefix) == namelist[n]->d_name) {
                if (++match > ABOX_DUMP_LIMIT) {
                    char *tgt;

                    if (asprintf(&tgt, "%s/%s", path, namelist[n]->d_name) != -1) {
                        remove(tgt);
                        free(tgt);
                    }
                }
            }
            free(namelist[n]);
        }
        free(namelist);
    }

    return ;
}

static void __calliope_dump(int fd, const char *in_prefix, const char *in_file, const char *out_prefix, const char *out_suffix)
{
    static const int buf_size = 4096;
    char *buf, in_path[128], out_path[128];
    int fd_in = -1, fd_out = -1, n;
    mode_t mask;

    ALOGV("proxy-%s", __func__);

    if (snprintf(in_path, sizeof(in_path) - 1, "%s%s", in_prefix, in_file) < 0) {
        ALOGE("proxy-%s: in path error: %s", __func__, strerror(errno));
        return;
    }

    if (snprintf(out_path, sizeof(out_path) - 1, "%s%s_%s.bin", out_prefix, in_file, out_suffix) < 0) {
        ALOGE("proxy-%s: out path error: %s", __func__, strerror(errno));
        return;
    }

    buf = malloc(buf_size);
    if (!buf) {
        ALOGE("proxy-%s: malloc failed: %s", __func__, strerror(errno));
        return;
    }

    mask = umask(0);
    ALOGV("umask = %o", mask);

    fd_in = open(in_path, O_RDONLY | O_NONBLOCK);
    if (fd_in < 0)
        ALOGE("proxy-%s: open error: %s, fd_in=%s", __func__, strerror(errno), in_path);

    fd_out = open(out_path, O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
    if (fd_out < 0)
        ALOGE("proxy-%s: open error: %s, fd_out=%s", __func__, strerror(errno), out_path);

    if (fd_in >= 0 && fd_out >= 0) {
        while((n = read(fd_in, buf, buf_size)) > 0) {
            if (write(fd_out, buf, n) < 0) {
                ALOGE("proxy-%s: write error: %s", __func__, strerror(errno));
            }
        }
        n = snprintf(buf, buf_size, " %s_%s.bin <= %s\n", in_file, out_suffix, in_file);
        write(fd, buf, n);
        ALOGI("proxy-%s", buf);
    }

    calliope_cleanup_old(out_prefix, in_file);

    if (fd_in >= 0)
        close(fd_in);
    if (fd_out >= 0)
        close(fd_out);

    mask = umask(mask);
    free(buf);

    return;
}

static void calliope_ramdump(int fd)
{
    char str_time[32];
    time_t t;
    struct tm *lt;

    ALOGD("%s", __func__);

    t = time(NULL);
    lt = localtime(&t);
    if (lt == NULL) {
        ALOGE("%s: time conversion error: %s", __func__, strerror(errno));
        return;
    }
    if (strftime(str_time, sizeof(str_time), "%Y%m%d_%H%M%S", lt) == 0) {
        ALOGE("%s: time error: %s", __func__, strerror(errno));
    }

    write(fd, "\n", strlen("\n"));
    write(fd, "Calliope snapshot:\n", strlen("Calliope snapshot:\n"));
    ALOGI("Calliope snapshot:\n");
    __calliope_dump(fd, SYSFS_PREFIX ABOX_DEV ABOX_DEBUG, ABOX_GPR, ABOX_DUMP, str_time);
    __calliope_dump(fd, CALLIOPE_DBG_PATH, CALLIOPE_LOG, ABOX_DUMP, str_time);
    __calliope_dump(fd, SYSFS_PREFIX ABOX_DEV ABOX_DEBUG, ABOX_SRAM, ABOX_DUMP, str_time);
    __calliope_dump(fd, SYSFS_PREFIX ABOX_DEV ABOX_DEBUG, ABOX_DRAM, ABOX_DUMP, str_time);
    __calliope_dump(fd, SYSFS_PREFIX ABOX_DEV ABOX_DEBUG, ABOX_SLOG, ABOX_DUMP, str_time);
#ifndef NO_USE_DEBUGFS
    __calliope_dump(fd, ABOX_REGMAP_PATH, ABOX_REG_FILE, ABOX_DUMP, str_time);
#endif
    write(fd, "Calliope snapshot done\n", strlen("Calliope snapshot done\n"));

    return ;
}

/******************************************************************************/
/**                                                                          **/
/** Local Functions for Audio Stream Proxy                                   **/
/**                                                                          **/
/******************************************************************************/
/* Compress Offload Specific Functions */
static bool is_supported_compressed_format(audio_format_t format)
{
    switch (format & AUDIO_FORMAT_MAIN_MASK) {
    case AUDIO_FORMAT_MP3:
    case AUDIO_FORMAT_AAC:
    case AUDIO_FORMAT_FLAC:
        return true;
    default:
        break;
    }

    return false;
}

static int get_snd_codec_id(audio_format_t format)
{
    int id = 0;

    switch (format & AUDIO_FORMAT_MAIN_MASK) {
    case AUDIO_FORMAT_MP3:
        id = SND_AUDIOCODEC_MP3;
        break;
    case AUDIO_FORMAT_AAC:
        id = SND_AUDIOCODEC_AAC;
        break;
    case AUDIO_FORMAT_FLAC:
        id = SND_AUDIOCODEC_FLAC;
        break;
    default:
            ALOGE("offload_out-%s: Unsupported audio format", __func__);
    }

    return id;
}

static void save_written_frames(struct audio_proxy_stream *apstream, int bytes)
{
    apstream->frames += bytes / (apstream->pcmconfig.channels *
                audio_bytes_per_sample(audio_format_from_pcm_format(apstream->pcmconfig.format)));
    ALOGVV("%s-%s: written = %u frames", stream_table[apstream->stream_type], __func__,
                                         (unsigned int)apstream->frames);
    return ;
}

static void skip_pcm_processing(struct audio_proxy_stream *apstream, int bytes)
{
    unsigned int frames = 0;

    frames = bytes / (apstream->pcmconfig.channels *
             audio_bytes_per_sample(audio_format_from_pcm_format(apstream->pcmconfig.format)));
    usleep(frames * 1000000 / proxy_get_actual_sampling_rate(apstream));
    return ;
}

static void update_capture_pcmconfig(struct audio_proxy_stream *apstream)
{
    int i;

    // Check Sampling Rate
    for (i = 0; i < MAX_NUM_CAPTURE_SR; i++) {
        if (apstream->requested_sample_rate == supported_capture_samplingrate[i]) {
            if (apstream->requested_sample_rate != apstream->pcmconfig.rate) {
                apstream->pcmconfig.rate = apstream->requested_sample_rate;
                if (apstream->stream_type == ASTREAM_CAPTURE_PRIMARY)
                    apstream->pcmconfig.period_size = (apstream->pcmconfig.rate * PREDEFINED_MEDIA_CAPTURE_DURATION) / 1000;
                else if (apstream->stream_type == ASTREAM_CAPTURE_LOW_LATENCY)
                    apstream->pcmconfig.period_size = (apstream->pcmconfig.rate * PREDEFINED_LOW_CAPTURE_DURATION) / 1000;

                // WDMA in A-Box is 128-bit aligned, so period_size has to be multiple of 4 frames
                apstream->pcmconfig.period_size &= 0xFFFFFFFC;
                ALOGD("%s-%s: updates samplig rate to %u, period_size to %u", stream_table[apstream->stream_type],
                                                           __func__, apstream->pcmconfig.rate,
                                                           apstream->pcmconfig.period_size);
            }
            break;
        }
    }

    if (i == MAX_NUM_CAPTURE_SR)
        ALOGD("%s-%s: needs re-sampling to %u", stream_table[apstream->stream_type], __func__,
                                                apstream->requested_sample_rate);

    // Check Channel Mask
    for (i = 0; i < MAX_NUM_CAPTURE_CM; i++) {
        if (apstream->requested_channel_mask == supported_capture_channelmask[i]) {
            if (audio_channel_count_from_in_mask(apstream->requested_channel_mask)
                != apstream->pcmconfig.channels) {
                {
                    apstream->pcmconfig.channels = audio_channel_count_from_in_mask(apstream->requested_channel_mask);
                    ALOGD("%s-%s: updates channel count to %u", stream_table[apstream->stream_type],
                            __func__, apstream->pcmconfig.channels);
                }
            }
            break;
        }
    }

    if (i == MAX_NUM_CAPTURE_CM)
        ALOGD("%s-%s: needs re-channeling to %u from %u", stream_table[apstream->stream_type], __func__,
              audio_channel_count_from_in_mask(apstream->requested_channel_mask), apstream->pcmconfig.channels);

    // Check PCM Format
    for (i = 0; i < MAX_NUM_CAPTURE_PF; i++) {
        if (apstream->requested_format == supported_capture_pcmformat[i]) {
            if (pcm_format_from_audio_format(apstream->requested_format) != apstream->pcmconfig.format) {
                apstream->pcmconfig.format = pcm_format_from_audio_format(apstream->requested_format);
                ALOGD("%s-%s: updates PCM format to %d", stream_table[apstream->stream_type], __func__,
                                                         apstream->pcmconfig.format);
            }
            break;
        }
    }

    if (i == MAX_NUM_CAPTURE_PF)
        ALOGD("%s-%s: needs re-formating to 0x%x", stream_table[apstream->stream_type], __func__,
                                                   apstream->requested_format);

    return ;
}

// For Resampler
int proxy_get_requested_frame_size(struct audio_proxy_stream *apstream)
{
    return audio_channel_count_from_in_mask(apstream->requested_channel_mask) *
           audio_bytes_per_sample(apstream->requested_format);
}

static int get_next_buffer(struct resampler_buffer_provider *buffer_provider,
                           struct resampler_buffer* buffer)
{
    struct audio_proxy_stream *apstream;

    if (buffer_provider == NULL || buffer == NULL)
        return -EINVAL;

    apstream = (struct audio_proxy_stream *)((char *)buffer_provider -
                                             offsetof(struct audio_proxy_stream, buf_provider));

    if (apstream->pcm) {
        if (apstream->read_buf_frames == 0) {
            unsigned int size_in_bytes = pcm_frames_to_bytes(apstream->pcm, apstream->pcmconfig.period_size);
            if (apstream->actual_read_buf_size < size_in_bytes) {
                apstream->actual_read_buf_size = size_in_bytes;
                apstream->actual_read_buf = (int16_t *) realloc(apstream->actual_read_buf, size_in_bytes);
                if (apstream->actual_read_buf != NULL)
                    ALOGI("%s-%s: alloc actual read buffer with %u bytes",
                           stream_table[apstream->stream_type], __func__, size_in_bytes);
            }

            if (apstream->actual_read_buf != NULL) {
                apstream->actual_read_status = pcm_read(apstream->pcm, (void*)apstream->actual_read_buf, size_in_bytes);
                if (apstream->actual_read_status != 0) {
                    ALOGE("%s-%s: pcm_read error %d(%s)", stream_table[apstream->stream_type],
                        __func__, apstream->actual_read_status, pcm_get_error(apstream->pcm));
                    buffer->raw = NULL;
                    buffer->frame_count = 0;
                    return apstream->actual_read_status;
                }
#ifdef SEC_AUDIO_DUMP
                if (apstream->pcmconfig.format != PCM_FORMAT_S24_LE)
                    in_get_pcm_dump(apstream, apstream->actual_read_buf, size_in_bytes, PCM_DUMP_PURE);
#endif

                if (apstream->stream_type == ASTREAM_CAPTURE_CALL) {
                    /*
                     * [Call Recording Case]
                     * In case of Call Recording, A-Box sends stereo stream which uplink/downlink voice
                     * allocated in left/right to AudioHAL.
                     * AudioHAL has to select and mix uplink/downlink voice from left/right channel as usage.
                     */
                    int16_t data_mono;
                    int16_t *vc_buf = (int16_t *)(apstream->actual_read_buf);

                    // Channel Selection
                    // output : Stereo with Left/Right contains same selected channel PCM & Device SR
                    for (unsigned int i = 0; i < apstream->pcmconfig.period_size; i++){
                        if (apstream->stream_usage == AUSAGE_INCALL_UPLINK)
                            data_mono = (*(vc_buf + 2*i + 1)); // Tx
                        else if (apstream->stream_usage == AUSAGE_INCALL_DOWNLINK){
                            data_mono = (*(vc_buf + 2*i));     // Rx
                        } else {
                            data_mono = clamp16(((int32_t)*(vc_buf+2*i) + (int32_t)*(vc_buf+2*i+1))*0.7); // mix Rx/Tx
                        }

                        *(vc_buf + 2*i)     = data_mono;
                        *(vc_buf + 2*i + 1) = data_mono;
                    }
#ifdef SEC_AUDIO_DUMP
                    in_get_pcm_dump(apstream, apstream->actual_read_buf, size_in_bytes, PCM_DUMP_CALLREC);
#endif
                }

                /* Convert A-Box's zero padded 24bit format to signed extension */
                if (apstream->pcmconfig.format == PCM_FORMAT_S24_LE) {
                    int *rd_buf = (int *)(apstream->actual_read_buf);

                    for (unsigned int i = 0;
                        i < (apstream->pcmconfig.period_size * apstream->pcmconfig.channels); i++) {
                        if (*(rd_buf + i) & 0x800000)
                            *(rd_buf + i) |= 0xFF000000;
                    }
#ifdef SEC_AUDIO_DUMP
                    in_get_pcm_dump(apstream, apstream->actual_read_buf, size_in_bytes, PCM_DUMP_PURE);
#endif
                }
#ifdef SEC_AUDIO_RESAMPLER
                if (apstream->resampler) {
                    apstream->conversion_frames = PostProcessConvertorProcess(apstream->resampler,
                                                                              apstream->conversion_buffer,
                                                                              apstream->actual_read_buf,
                                                                              apstream->pcmconfig.period_size);
                    if (apstream->conversion_frames <= 0) {
                        ALOGE("%s-%s: resampler error %zd", stream_table[apstream->stream_type], __func__,
                                                           apstream->conversion_frames);
                        buffer->raw = NULL;
                        buffer->frame_count = 0;
                        apstream->actual_read_status = -EINVAL;
                        return apstream->actual_read_status;
                    }
                    size_in_bytes = apstream->conversion_frames
                                    * proxy_get_actual_channel_count(apstream)
                                    * audio_bytes_per_sample(proxy_get_last_format(apstream));
                    if (apstream->actual_read_buf_size < size_in_bytes) {
                        apstream->actual_read_buf_size = size_in_bytes;
                        apstream->actual_read_buf = (int16_t *) realloc(apstream->actual_read_buf, size_in_bytes);
                    }
                    memcpy(apstream->actual_read_buf, apstream->conversion_buffer, size_in_bytes);
                    apstream->read_buf_frames = apstream->conversion_frames;
                } else {
                    apstream->read_buf_frames = apstream->pcmconfig.period_size;
                    apstream->conversion_frames = apstream->read_buf_frames;
                }
                ALOGVV("%s: rate = %d to %d, frames = %d to %d, size %d", __func__,
                    apstream->pcmconfig.rate, apstream->requested_sample_rate,
                    apstream->pcmconfig.period_size, apstream->conversion_frames, size_in_bytes);
#else
                apstream->read_buf_frames = apstream->pcmconfig.period_size;
#endif
            } else {
                ALOGE("%s-%s: failed to reallocate actual_read_buf",
                      stream_table[apstream->stream_type], __func__);
                buffer->raw = NULL;
                buffer->frame_count = 0;
                apstream->actual_read_status = -ENOMEM;
                return -ENOMEM;
            }
        }

        buffer->frame_count = (buffer->frame_count > apstream->read_buf_frames) ?
                               apstream->read_buf_frames : buffer->frame_count;
#ifdef SEC_AUDIO_RESAMPLER
        buffer->i16 = apstream->actual_read_buf + (apstream->conversion_frames - apstream->read_buf_frames) *
                                                  apstream->pcmconfig.channels;
#else
        buffer->i16 = apstream->actual_read_buf + (apstream->pcmconfig.period_size - apstream->read_buf_frames) *
                                                  apstream->pcmconfig.channels;
#endif
        return apstream->actual_read_status;
    } else {
        buffer->raw = NULL;
        buffer->frame_count = 0;
        apstream->actual_read_status = -ENODEV;
        return -ENODEV;
    }
}

static void release_buffer(struct resampler_buffer_provider *buffer_provider,
                           struct resampler_buffer* buffer)
{
    struct audio_proxy_stream *apstream;

    if (buffer_provider == NULL || buffer == NULL)
        return;

    apstream = (struct audio_proxy_stream *)((char *)buffer_provider -
                                             offsetof(struct audio_proxy_stream, buf_provider));

    apstream->read_buf_frames -= buffer->frame_count;
}

static int read_frames(struct audio_proxy_stream *apstream, void *buffer, int frames)
{
    int frames_wr = 0;

    while (frames_wr < frames) {
        size_t frames_rd = frames - frames_wr;
        ALOGVV("%s-%s: frames_rd: %zd, frames_wr: %d",
           stream_table[apstream->stream_type], __func__, frames_rd, frames_wr);

#ifndef SEC_AUDIO_RESAMPLER
        if (apstream->resampler != NULL) {
            apstream->resampler->resample_from_provider(apstream->resampler,
            (int16_t *)((char *)buffer + pcm_frames_to_bytes(apstream->pcm, frames_wr)), &frames_rd);
        } else
#endif
        {
            struct resampler_buffer buf;
            buf.raw= NULL;
            buf.frame_count = frames_rd;

            get_next_buffer(&apstream->buf_provider, &buf);
            if (buf.raw != NULL) {
#ifdef SEC_AUDIO_RESAMPLER
                unsigned int frames_size = proxy_get_actual_channel_count(apstream)
                                           * audio_bytes_per_sample(proxy_get_last_format(apstream));
                memcpy((char *)buffer + frames_wr * frames_size,
                        buf.raw, (unsigned int)(buf.frame_count) * frames_size);
#else
                memcpy((char *)buffer + pcm_frames_to_bytes(apstream->pcm, frames_wr),
                        buf.raw, pcm_frames_to_bytes(apstream->pcm, buf.frame_count));
#endif
                frames_rd = buf.frame_count;
            }
            release_buffer(&apstream->buf_provider, &buf);
        }

        /* apstream->actual_read_status is updated by getNextBuffer() also called by
         * apstream->resampler->resample_from_provider() */
        if (apstream->actual_read_status != 0)
            return apstream->actual_read_status;

        frames_wr += frames_rd;
    }

    return frames_wr;
}

static int read_and_process_frames(struct audio_proxy_stream *apstream, void* buffer, int frames_num)
{
    int frames_wr = 0;
#ifdef SEC_AUDIO_SAMSUNGRECORD
    unsigned int bytes_per_sample = audio_bytes_per_sample(proxy_get_last_format(apstream));
#else
    unsigned int bytes_per_sample = (pcm_format_to_bits(apstream->pcmconfig.format) >> 3);
#endif
    void *proc_buf_out = buffer;

    int num_device_channels = proxy_get_actual_channel_count(apstream);
    int num_req_channels = audio_channel_count_from_in_mask(apstream->requested_channel_mask);

    /* Prepare Channel Conversion Input Buffer */
    if (apstream->need_channelconversion && (num_device_channels != num_req_channels)) {
        int src_buffer_size = frames_num * num_device_channels * bytes_per_sample;

        if (apstream->proc_buf_size < src_buffer_size) {
            apstream->proc_buf_size = src_buffer_size;
            apstream->proc_buf_out = realloc(apstream->proc_buf_out, src_buffer_size);
            ALOGI("%s-%s: alloc resampled read buffer with %d bytes",
                      stream_table[apstream->stream_type], __func__, src_buffer_size);
        }
        proc_buf_out = apstream->proc_buf_out;
    }

    frames_wr = read_frames(apstream, proc_buf_out, frames_num);
    if ((frames_wr > 0) && (frames_wr > frames_num))
        ALOGE("%s-%s: read more frames than requested", stream_table[apstream->stream_type], __func__);

    /*
     * A-Box can support only Stereo channel, not Mono channel.
     * If platform wants Mono Channel Recording, AudioHAL has to support mono conversion.
     */
    if (apstream->actual_read_status == 0) {
        if (apstream->need_channelconversion && (num_device_channels != num_req_channels)) {
            size_t ret = adjust_channels(proc_buf_out, num_device_channels,
                                         buffer, num_req_channels,
                                         bytes_per_sample, (frames_wr * num_device_channels * bytes_per_sample));
            if (ret != (frames_wr * num_req_channels * bytes_per_sample))
                ALOGE("%s-%s: channel convert failed", stream_table[apstream->stream_type], __func__);
        }
#ifdef SEC_AUDIO_DUMP
        int channel_cnt = apstream->skip_ch_convert ? num_device_channels : num_req_channels;
        in_get_pcm_dump(apstream, buffer, (frames_wr * channel_cnt * bytes_per_sample), PCM_DUMP_CONVERT);
#endif
    } else {
        ALOGE("%s-%s: Read Fail = %d", stream_table[apstream->stream_type], __func__, frames_wr);
    }

    return frames_wr;
}

static void check_conversion(struct audio_proxy_stream *apstream)
{
    int request_cc = audio_channel_count_from_in_mask(apstream->requested_channel_mask);

    // Check Mono/stereo Conversion is needed or not
    if ((request_cc == MEDIA_1_CHANNEL && apstream->pcmconfig.channels == DEFAULT_MEDIA_CHANNELS)
        || (request_cc == DEFAULT_MEDIA_CHANNELS && apstream->pcmconfig.channels == MEDIA_1_CHANNEL)
#ifdef SEC_AUDIO_SAMSUNGRECORD
        && !apstream->skip_ch_convert
#endif
        ) {
        // enable channel Conversion
        apstream->need_channelconversion = true;
        ALOGD("%s-%s: needs re-channeling to %u from %u", stream_table[apstream->stream_type], __func__,
              request_cc, apstream->pcmconfig.channels);
    }

#ifdef SEC_AUDIO_RESAMPLER
    // Reset for reconfig case
    if (apstream->resampler) {
        PostProcessConvertorClear(apstream->resampler);
        apstream->resampler = NULL;
    }
    audio_format_t proxy_last_format = proxy_get_last_format(apstream);
    apstream->read_buf_frames = 0;
    apstream->conversion_frames = 0;

    // Check Re-Sampler is needed or not
    if ((apstream->requested_sample_rate &&
        apstream->requested_sample_rate != apstream->pcmconfig.rate) ||
        proxy_last_format != audio_format_from_pcm_format(apstream->pcmconfig.format)) {
        // Only support Stereo Resampling
        apstream->resampler = PostProcessConvertorInit(apstream->requested_sample_rate,
                                                       apstream->pcmconfig.rate,
                                                       apstream->pcmconfig.channels,
                                                       proxy_last_format,
                                                       audio_format_from_pcm_format(apstream->pcmconfig.format));
        if (apstream->resampler == NULL) {
            ALOGE("proxy-%s: failed to create resampler", __func__);
        } else {
            size_t frames_cv = (apstream->pcmconfig.period_size * apstream->requested_sample_rate)
                               / apstream->pcmconfig.rate + 1;
            size_t size_in_bytes = frames_cv * proxy_get_actual_channel_count(apstream)
                                   * audio_bytes_per_sample(proxy_last_format);
            if (apstream->conversion_buffer_size < size_in_bytes) {
                apstream->conversion_buffer_size = size_in_bytes;
                apstream->conversion_buffer = (int16_t *) realloc(apstream->conversion_buffer, size_in_bytes);
                if (apstream->conversion_buffer == NULL)
                    ALOGE("%s-%s: failed to realloc conversion_buffer",
                           stream_table[apstream->stream_type], __func__);
                else
                    ALOGI("%s-%s: realloc conversion_buffer with %d bytes",
                           stream_table[apstream->stream_type], __func__, (int)size_in_bytes);
            }
            ALOGV("proxy-%s: resampler created in-samplerate %d out-samplereate %d",
                  __func__, apstream->pcmconfig.rate, apstream->requested_sample_rate);
        }
    }
#else
    if (apstream->requested_sample_rate &&
        apstream->requested_sample_rate != apstream->pcmconfig.rate) {
        // Only support Stereo Resampling
        if (apstream->resampler) {
            release_resampler(apstream->resampler);
            apstream->resampler = NULL;
        }

        apstream->buf_provider.get_next_buffer = get_next_buffer;
        apstream->buf_provider.release_buffer = release_buffer;
        int ret = create_resampler(apstream->pcmconfig.rate, apstream->requested_sample_rate,
                                   apstream->pcmconfig.channels, RESAMPLER_QUALITY_DEFAULT,
                                   &apstream->buf_provider, &apstream->resampler);
        if (ret !=0) {
            ALOGE("proxy-%s: failed to create resampler", __func__);
        } else {
            ALOGV("proxy-%s: resampler created in-samplerate %d out-samplereate %d",
                  __func__, apstream->pcmconfig.rate, apstream->requested_sample_rate);

            apstream->need_resampling = true;
            ALOGD("%s-%s: needs re-sampling to %u Hz from %u Hz", stream_table[apstream->stream_type], __func__,
                  apstream->requested_sample_rate, apstream->pcmconfig.rate);

            apstream->actual_read_buf = NULL;
            apstream->actual_read_buf_size = 0;
            apstream->read_buf_frames = 0;

            apstream->resampler->reset(apstream->resampler);
        }
    }
#endif

    return ;
}

/*
 * Modify config->period_count based on min_size_frames
 */
static void adjust_mmap_period_count(struct audio_proxy_stream *apstream, struct pcm_config *config,
                                     int32_t min_size_frames)
{
    int periodCountRequested = (min_size_frames + config->period_size - 1)
                               / config->period_size;
    int periodCount = MMAP_PERIOD_COUNT_MIN;

    ALOGV("%s-%s: original config.period_size = %d config.period_count = %d",
          stream_table[apstream->stream_type], __func__, config->period_size, config->period_count);

    while (periodCount < periodCountRequested && (periodCount * 2) < MMAP_PERIOD_COUNT_MAX) {
        periodCount *= 2;
    }
    config->period_count = periodCount;

    ALOGV("%s-%s: requested config.period_count = %d", stream_table[apstream->stream_type], __func__,
                                                       config->period_count);
}

int get_mmap_data_fd(void *proxy_stream, audio_usage_type usage_type,
                                                            int *fd, unsigned int *size)
{
    struct audio_proxy_stream *apstream = (struct audio_proxy_stream *)proxy_stream;
    struct snd_pcm_mmap_fd mmapfd_info;
    char dev_name[128];
    int hw_fd = -1;
    int ret = 0;
    int hwdev_node = -1;

    memset(&mmapfd_info, 0, sizeof(mmapfd_info));
    mmapfd_info.dir = usage_type;

    // get MMAP device node number based on usage direction
    hwdev_node = ((usage_type ==  AUSAGE_PLAYBACK) ? MMAP_PLAYBACK_DEVICE :
                            MMAP_CAPTURE_DEVICE);
    snprintf(dev_name, sizeof(dev_name), "/dev/snd/hwC0D%d", hwdev_node);
    hw_fd = open(dev_name, O_RDONLY);
    if (hw_fd < 0) {
        ALOGE("%s: hw %s node open failed", __func__, dev_name);
        ret = -1;
        goto err;
    }

    // get mmap fd for exclusive mode
    if (ioctl(hw_fd, SNDRV_PCM_IOCTL_MMAP_DATA_FD, &mmapfd_info) < 0) {
        ALOGE("%s-%s: get MMAP FD IOCTL failed",
                  stream_table[apstream->stream_type], __func__);
        ret = -1;
        goto err;
    }
    *fd = mmapfd_info.fd;
    *size = mmapfd_info.size;

err:
    if (hw_fd >= 0)
        close(hw_fd);
    return ret;
}


/******************************************************************************/
/**                                                                          **/
/** Interfaces for Audio Stream Proxy                                        **/
/**                                                                          **/
/******************************************************************************/

void proxy_set_call_path_param(uint32_t set, uint32_t param, int32_t value)
{
    struct audio_proxy *aproxy = getInstance();

    ALOGI("proxy-%s: enter with param: %d val: %d", __func__, param, value);

    if (param == CALL_BAND) {
        aproxy->call_param_idx.rate_band = value;
        ALOGI("proxy-%s: update Band param(%d)", __func__, value);
    } else if (param == CALL_ROT) {  // change call type value format from Ril to firmware
        aproxy->call_param_idx.call_type = value;
        ALOGI("proxy-%s: update call-type param(%d)", __func__, value);
    } else if (param == RX_DEVICE) {
        aproxy->call_param_idx.device = value;
        ALOGI("proxy-%s: update device param(%d)", __func__, value);
    }

    if (set == CALL_PATH_SET) {
        ALOGI("proxy-%s: set Call path parameter idx(0x%x)", __func__, param);
        set_call_path_param();
    }

    return ;
}

uint32_t proxy_get_actual_channel_count(void *proxy_stream)
{
    struct audio_proxy_stream *apstream = (struct audio_proxy_stream *)proxy_stream;
    uint32_t actual_channel_count = 0;

    if (apstream) {
        if (apstream->stream_type == ASTREAM_PLAYBACK_COMPR_OFFLOAD)
            actual_channel_count = (uint32_t)audio_channel_count_from_out_mask(apstream->comprconfig.codec->ch_in);
        else
            actual_channel_count = (uint32_t)apstream->pcmconfig.channels;
    }

    return actual_channel_count;
}

uint32_t proxy_get_actual_sampling_rate(void *proxy_stream)
{
    struct audio_proxy_stream *apstream = (struct audio_proxy_stream *)proxy_stream;
    uint32_t actual_sampling_rate = 0;

    if (apstream) {
        if (apstream->stream_type == ASTREAM_PLAYBACK_COMPR_OFFLOAD)
            actual_sampling_rate = (uint32_t)apstream->comprconfig.codec->sample_rate;
        else
            actual_sampling_rate = (uint32_t)apstream->pcmconfig.rate;
    }

    return actual_sampling_rate;
}

uint32_t proxy_get_actual_period_size(void *proxy_stream)
{
    struct audio_proxy_stream *apstream = (struct audio_proxy_stream *)proxy_stream;
    uint32_t actual_period_size = 0;

    if (apstream) {
        if (apstream->stream_type == ASTREAM_PLAYBACK_COMPR_OFFLOAD)
            actual_period_size = (uint32_t)apstream->comprconfig.fragment_size;
        else
            actual_period_size = (uint32_t)apstream->pcmconfig.period_size;
    }

    return actual_period_size;
}

uint32_t proxy_get_actual_period_count(void *proxy_stream)
{
    struct audio_proxy_stream *apstream = (struct audio_proxy_stream *)proxy_stream;
    uint32_t actual_period_count = 0;

    if (apstream) {
        if (apstream->stream_type == ASTREAM_PLAYBACK_COMPR_OFFLOAD)
            actual_period_count = (uint32_t)apstream->comprconfig.fragments;
        else
            actual_period_count = (uint32_t)apstream->pcmconfig.period_count;
    }

    return actual_period_count;
}

int32_t proxy_get_actual_format(void *proxy_stream)
{
    struct audio_proxy_stream *apstream = (struct audio_proxy_stream *)proxy_stream;
    int32_t actual_format = (int32_t)AUDIO_FORMAT_INVALID;

    if (apstream) {
        if (apstream->stream_type == ASTREAM_PLAYBACK_COMPR_OFFLOAD)
            actual_format = (int32_t)apstream->comprconfig.codec->format;
        else
            actual_format = (int32_t)audio_format_from_pcm_format(apstream->pcmconfig.format);
    }

    return actual_format;
}

void  proxy_offload_set_nonblock(void *proxy_stream)
{
    struct audio_proxy_stream *apstream = (struct audio_proxy_stream *)proxy_stream;

    if (apstream->stream_type == ASTREAM_PLAYBACK_COMPR_OFFLOAD)
        apstream->nonblock_flag = 1;

    return ;
}

int proxy_offload_compress_func(void *proxy_stream, int func_type)
{
    struct audio_proxy_stream *apstream = (struct audio_proxy_stream *)proxy_stream;
    int ret = 0;

    if (apstream->stream_type == ASTREAM_PLAYBACK_COMPR_OFFLOAD) {
        if (apstream->compress) {
            switch (func_type) {
                case COMPRESS_TYPE_WAIT:
                    ret = compress_wait(apstream->compress, -1);
                    ALOGVV("%s-%s: returned from waiting", stream_table[apstream->stream_type], __func__);
                    break;

                case COMPRESS_TYPE_NEXTTRACK:
                    ret = compress_next_track(apstream->compress);
                    ALOGI("%s-%s: set next track", stream_table[apstream->stream_type], __func__);
                    break;

                case COMPRESS_TYPE_PARTIALDRAIN:
                    ret = compress_partial_drain(apstream->compress);
                    ALOGI("%s-%s: drained this track partially", stream_table[apstream->stream_type], __func__);

                    /* Resend the metadata for next iteration */
                    apstream->ready_new_metadata = 1;
                    break;

                case COMPRESS_TYPE_DRAIN:
                    ret = compress_drain(apstream->compress);
                    ALOGI("%s-%s: drained this track", stream_table[apstream->stream_type], __func__);
                    break;

                default:
                    ALOGE("%s-%s: unsupported Offload Compress Function(%d)",
                           stream_table[apstream->stream_type], __func__, func_type);
            }
        }
    }

    return ret;
}

int proxy_offload_pause(void *proxy_stream)
{
    struct audio_proxy_stream *apstream = (struct audio_proxy_stream *)proxy_stream;
    int ret = 0;

    if (apstream->stream_type == ASTREAM_PLAYBACK_COMPR_OFFLOAD) {
        if (apstream->compress) {
            ret = compress_pause(apstream->compress);
            ALOGV("%s-%s: paused compress offload!", stream_table[apstream->stream_type], __func__);
        }
    }

    return ret;
}

int proxy_offload_resume(void *proxy_stream)
{
    struct audio_proxy_stream *apstream = (struct audio_proxy_stream *)proxy_stream;
    int ret = 0;

    if (apstream->stream_type == ASTREAM_PLAYBACK_COMPR_OFFLOAD) {
        if (apstream->compress) {
            ret = compress_resume(apstream->compress);
            ALOGV("%s-%s: resumed compress offload!", stream_table[apstream->stream_type], __func__);
        }
    }

    return ret;
}


void *proxy_create_playback_stream(void *proxy, int type, void *config, char *address __unused)
{
    struct audio_proxy *aproxy = proxy;
    audio_stream_type stream_type = (audio_stream_type)type;
    struct audio_config *requested_config = (struct audio_config *)config;

    struct audio_proxy_stream *apstream;

    apstream = (struct audio_proxy_stream *)calloc(1, sizeof(struct audio_proxy_stream));
    if (!apstream) {
        ALOGE("proxy-%s: failed to allocate memory for Proxy Stream", __func__);
        return NULL;;
    }

    /* Stores the requested configurations. */
    apstream->requested_sample_rate = requested_config->sample_rate;
    apstream->requested_channel_mask = requested_config->channel_mask;
    apstream->requested_format = requested_config->format;

    apstream->stream_type = stream_type;
    apstream->need_update_pcm_config = false;

    /* Sets basic configuration from Stream Type. */
    switch (apstream->stream_type) {
        // For VTS
        case ASTREAM_PLAYBACK_NO_ATTRIBUTE:
            apstream->sound_card = PRIMARY_PLAYBACK_CARD;
            apstream->sound_device = PRIMARY_PLAYBACK_DEVICE;
            apstream->pcmconfig = pcm_config_primary_playback;

            break;

        case ASTREAM_PLAYBACK_PRIMARY:
            apstream->sound_card = PRIMARY_PLAYBACK_CARD;
            apstream->sound_device = get_pcm_device_number(aproxy, apstream);
            apstream->pcmconfig = pcm_config_primary_playback;

            if (aproxy->primary_out == NULL)
                aproxy->primary_out = apstream;
            else
                ALOGE("proxy-%s: Primary Output Proxy Stream is already created!!!", __func__);
            break;

        case ASTREAM_PLAYBACK_DEEP_BUFFER:
            apstream->sound_card = DEEP_PLAYBACK_CARD;
            apstream->sound_device = get_pcm_device_number(aproxy, apstream);
            apstream->pcmconfig = pcm_config_deep_playback;
            break;

        case ASTREAM_PLAYBACK_FAST:
            apstream->sound_card = FAST_PLAYBACK_CARD;
            apstream->sound_device = get_pcm_device_number(aproxy, apstream);
            apstream->pcmconfig = pcm_config_fast_playback;
            break;

        case ASTREAM_PLAYBACK_LOW_LATENCY:
            apstream->sound_card = LOW_PLAYBACK_CARD;
            apstream->sound_device = get_pcm_device_number(aproxy, apstream);
            apstream->pcmconfig = pcm_config_low_playback;
            break;

        case ASTREAM_PLAYBACK_COMPR_OFFLOAD:
            apstream->sound_card = OFFLOAD_PLAYBACK_CARD;
            apstream->sound_device = get_pcm_device_number(aproxy, apstream);
            apstream->comprconfig = compr_config_offload_playback;
            /* dummy primary pcmconfig used for best match selection */
            apstream->pcmconfig = pcm_config_primary_playback;

            if (is_supported_compressed_format(requested_config->offload_info.format)) {
                apstream->comprconfig.codec = (struct snd_codec *)calloc(1, sizeof(struct snd_codec));
                if (apstream->comprconfig.codec == NULL) {
                    ALOGE("proxy-%s: fail to allocate memory for Sound Codec", __func__);
                    goto err_open;
                }

                apstream->comprconfig.codec->id = get_snd_codec_id(requested_config->offload_info.format);
                apstream->comprconfig.codec->ch_in = requested_config->channel_mask;
                apstream->comprconfig.codec->ch_out = requested_config->channel_mask;
                apstream->comprconfig.codec->sample_rate = requested_config->sample_rate;
                apstream->comprconfig.codec->bit_rate = requested_config->offload_info.bit_rate;
                apstream->comprconfig.codec->format = requested_config->format;

                apstream->ready_new_metadata = 1;
            } else {
                ALOGE("proxy-%s: unsupported Compressed Format(%x)", __func__,
                                                            requested_config->offload_info.format);
                goto err_open;
            }
            break;

        case ASTREAM_PLAYBACK_MMAP:
            apstream->sound_card = MMAP_PLAYBACK_CARD;
            apstream->sound_device = get_pcm_device_number(aproxy, apstream);
            apstream->pcmconfig = pcm_config_mmap_playback;

            break;

        case ASTREAM_PLAYBACK_AUX_DIGITAL:
            apstream->sound_card = AUX_PLAYBACK_CARD;
            apstream->sound_device = get_pcm_device_number(aproxy, apstream);
            apstream->pcmconfig = pcm_config_aux_playback;

            if (apstream->requested_sample_rate != 0) {
                apstream->pcmconfig.rate = apstream->requested_sample_rate;
                // It needs Period Size adjustment based with predefined duration
                // to avoid underrun noise by small buffer at high sampling rate
                if (apstream->requested_sample_rate > DEFAULT_MEDIA_SAMPLING_RATE) {
                    apstream->pcmconfig.period_size = (apstream->requested_sample_rate * PREDEFINED_DP_PLAYBACK_DURATION) / 1000;
                    ALOGI("proxy-%s: changed Period Size(%d) as requested sampling rate(%d)",
                          __func__, apstream->pcmconfig.period_size, apstream->pcmconfig.rate);
                }
            }
            if (apstream->requested_channel_mask != AUDIO_CHANNEL_NONE) {
                apstream->pcmconfig.channels = audio_channel_count_from_out_mask(apstream->requested_channel_mask);
            }
            if (apstream->requested_format != AUDIO_FORMAT_DEFAULT) {
                apstream->pcmconfig.format = pcm_format_from_audio_format(apstream->requested_format);
            }

            break;

        default:
            ALOGE("proxy-%s: failed to open Proxy Stream as unknown stream type(%d)", __func__,
                                                                          apstream->stream_type);
            goto err_open;
    }

    apstream->pcm = NULL;
    apstream->compress = NULL;

    ALOGI("proxy-%s: opened Proxy Stream(%s)", __func__, stream_table[apstream->stream_type]);
    return (void *)apstream;

err_open:
    free(apstream);
    return NULL;
}

void proxy_destroy_playback_stream(void *proxy_stream)
{
    struct audio_proxy *aproxy = getInstance();
    struct audio_proxy_stream *apstream = (struct audio_proxy_stream *)proxy_stream;

    if (apstream) {
        if (apstream->stream_type == ASTREAM_PLAYBACK_COMPR_OFFLOAD) {
            if (apstream->comprconfig.codec != NULL)
                free(apstream->comprconfig.codec);
        }

        if (apstream->stream_type == ASTREAM_PLAYBACK_PRIMARY) {
            if (aproxy->primary_out != NULL)
                aproxy->primary_out = NULL;
        }

#ifdef SEC_AUDIO_DUMP
        out_stop_pcm_dump(apstream);
#endif
        if (apstream->proc_buf_out)
            free(apstream->proc_buf_out);

        free(apstream);
    }

    return ;
}

int proxy_close_playback_stream(void *proxy_stream)
{
    struct audio_proxy_stream *apstream = (struct audio_proxy_stream *)proxy_stream;
    int ret = 0;

    /* Close Noamrl PCM Device */
    if (apstream->stream_type == ASTREAM_PLAYBACK_COMPR_OFFLOAD) {
        if (apstream->compress) {
            compress_close(apstream->compress);
            apstream->compress = NULL;
        }
        ALOGI("%s-%s: closed Compress Device", stream_table[apstream->stream_type], __func__);
    } else {
        if (apstream->pcm) {
            ret = pcm_close(apstream->pcm);
            apstream->pcm = NULL;
        }
        if (apstream->dma_pcm) {
            pcm_close(apstream->dma_pcm);
            apstream->dma_pcm = NULL;
        }
        ALOGI("%s-%s: closed PCM Device", stream_table[apstream->stream_type], __func__);
    }

    return ret;
}

int proxy_open_playback_stream(void *proxy_stream, int32_t min_size_frames, void *mmap_info)
{
    struct audio_proxy_stream *apstream = (struct audio_proxy_stream *)proxy_stream;
    struct audio_proxy *aproxy = getInstance();
    struct audio_mmap_buffer_info *info = (struct audio_mmap_buffer_info *)mmap_info;
    unsigned int sound_card;
    unsigned int sound_device;
    unsigned int flags;
    int ret = 0;
    char pcm_path[MAX_PCM_PATH_LEN];

    /* Get PCM/Compress Device */
    sound_card = apstream->sound_card;
    sound_device = apstream->sound_device;

    /* Open Normal PCM Device */
    if (apstream->stream_type == ASTREAM_PLAYBACK_COMPR_OFFLOAD) {
        if (apstream->compress == NULL) {
            flags = COMPRESS_IN;

            apstream->compress = compress_open(sound_card, sound_device, flags, &apstream->comprconfig);
            if (apstream->compress && !is_compress_ready(apstream->compress)) {
                /* compress_open does always return compress structure, not NULL */
                ALOGE("%s-%s: Compress Device is not ready with Sampling_Rate(%u) error(%s)!",
                      stream_table[apstream->stream_type], __func__, apstream->comprconfig.codec->sample_rate,
                      compress_get_error(apstream->compress));
                goto err_open;
            }

            snprintf(pcm_path, sizeof(pcm_path), "/dev/snd/comprC%uD%u", sound_card, sound_device);
            ALOGI("%s-%s: The opened Compress Device is %s with Sampling_Rate(%u) PCM_Format(%d) Fragment_Size(%u)",
                  stream_table[apstream->stream_type], __func__, pcm_path,
                  apstream->comprconfig.codec->sample_rate, apstream->comprconfig.codec->format,
                  apstream->comprconfig.fragment_size);

            apstream->pcm = NULL;
        }
    } else {
        if (apstream->pcm == NULL) {
            struct pcm_config *ppcmconfig = &apstream->pcmconfig;
            /* Deep playback pcm nodes are selected based on sample rate
             * - VPCMDAI node (Busy-Domain path) can support upto 48Khz
             * - RDMA0 node (direct access path) for rate above 48KHz
             * therefore before opening deep stream pcm node should be updated
             */
            if (apstream->stream_type == ASTREAM_PLAYBACK_DEEP_BUFFER) {
                sound_device = apstream->sound_device = get_pcm_device_number(aproxy, apstream);
            }

            if (apstream->stream_type == ASTREAM_PLAYBACK_MMAP) {
                flags = PCM_OUT | PCM_MMAP | PCM_NOIRQ | PCM_MONOTONIC;

                adjust_mmap_period_count(apstream, ppcmconfig, min_size_frames);
            } else
                flags = PCM_OUT | PCM_MONOTONIC;

            apstream->pcm = pcm_open(sound_card, sound_device, flags, ppcmconfig);
            if (apstream->pcm && !pcm_is_ready(apstream->pcm)) {
                /* pcm_open does always return pcm structure, not NULL */
                ALOGE("%s-%s: PCM Device is not ready with Sampling_Rate(%u) error(%s)!",
                      stream_table[apstream->stream_type], __func__, ppcmconfig->rate,
                      pcm_get_error(apstream->pcm));
                goto err_open;
            }

            snprintf(pcm_path, sizeof(pcm_path), "/dev/snd/pcmC%uD%u%c", sound_card, sound_device ,'p');
            ALOGI("%s-%s: The opened PCM Device is %s with Sampling_Rate(%u) PCM_Format(%d)  PCM_start-threshold(%d) PCM_stop-threshold(%d)",
                  stream_table[apstream->stream_type], __func__, pcm_path,
                  ppcmconfig->rate, ppcmconfig->format,
                  ppcmconfig->start_threshold, ppcmconfig->stop_threshold);

            apstream->compress = NULL;

            if (apstream->stream_type == ASTREAM_PLAYBACK_MMAP) {
                unsigned int offset1 = 0;
                unsigned int frames1 = 0;
                unsigned int buf_size = 0;
                unsigned int mmap_size = 0;

                ret = pcm_mmap_begin(apstream->pcm, &info->shared_memory_address, &offset1, &frames1);
                if (ret == 0)  {
                    ALOGI("%s-%s: PCM Device begin MMAP", stream_table[apstream->stream_type], __func__);

                    info->buffer_size_frames = pcm_get_buffer_size(apstream->pcm);
                    buf_size = pcm_frames_to_bytes(apstream->pcm, info->buffer_size_frames);
                    info->burst_size_frames = apstream->pcmconfig.period_size;
                    // get mmap buffer fd
                    ret = get_mmap_data_fd(proxy_stream, AUSAGE_PLAYBACK,
                                                            &info->shared_memory_fd, &mmap_size);
                    if (ret < 0) {
                        // Fall back to poll_fd mode, shared mode
                        info->shared_memory_fd = pcm_get_poll_fd(apstream->pcm);
                        ALOGI("%s-%s: PCM Device MMAP Exclusive mode not support",
                            stream_table[apstream->stream_type], __func__);
                    } else {
                        if (mmap_size < buf_size) {
                            ALOGE("%s-%s: PCM Device MMAP buffer size not matching",
                                  stream_table[apstream->stream_type], __func__);
                            goto err_open;
                        }
                        // FIXME: indicate exclusive mode support by returning a negative buffer size
                        info->buffer_size_frames *= -1;
                    }

                    memset(info->shared_memory_address, 0,
                           pcm_frames_to_bytes(apstream->pcm, info->buffer_size_frames));

                    ret = pcm_mmap_commit(apstream->pcm, 0, MMAP_PERIOD_SIZE);
                    if (ret < 0) {
                        ALOGE("%s-%s: PCM Device cannot commit MMAP with error(%s)",
                              stream_table[apstream->stream_type], __func__, pcm_get_error(apstream->pcm));
                        goto err_open;
                    } else {
                        ALOGI("%s-%s: PCM Device commit MMAP", stream_table[apstream->stream_type], __func__);
                        ret = 0;
                    }
                } else {
                    ALOGE("%s-%s: PCM Device cannot begin MMAP with error(%s)",
                          stream_table[apstream->stream_type], __func__, pcm_get_error(apstream->pcm));
                    goto err_open;
                }
            }
        } else
            ALOGW("%s-%s: PCM Device is already opened!", stream_table[apstream->stream_type], __func__);
    }

    apstream->need_update_pcm_config = false;

    return ret;

err_open:
    proxy_close_playback_stream(proxy_stream);
    return -ENODEV;
}

int proxy_start_playback_stream(void *proxy_stream)
{
    struct audio_proxy_stream *apstream = (struct audio_proxy_stream *)proxy_stream;
    int ret = 0;

    if (apstream->stream_type == ASTREAM_PLAYBACK_COMPR_OFFLOAD) {
        if (apstream->compress) {
            if (apstream->nonblock_flag) {
                compress_nonblock(apstream->compress, apstream->nonblock_flag);
                ALOGV("%s-%s: set Nonblock mode!", stream_table[apstream->stream_type], __func__);
            } else {
                compress_nonblock(apstream->compress, 0);
                ALOGV("%s-%s: set Block mode!", stream_table[apstream->stream_type], __func__);
            }

            ret = compress_start(apstream->compress);
            if (ret == 0)
                ALOGI("%s-%s: started Compress Device", stream_table[apstream->stream_type], __func__);
            else
                ALOGE("%s-%s: cannot start Compress Offload(%s)", stream_table[apstream->stream_type],
                                               __func__, compress_get_error(apstream->compress));
        } else
            ret = -ENOSYS;
    } else if (apstream->stream_type == ASTREAM_PLAYBACK_MMAP) {
        if (apstream->pcm) {
            ret = pcm_start(apstream->pcm);
            if (ret == 0)
                ALOGI("%s-%s: started MMAP Device", stream_table[apstream->stream_type], __func__);
            else
                ALOGE("%s-%s: cannot start MMAP device with error(%s)", stream_table[apstream->stream_type],
                                               __func__, pcm_get_error(apstream->pcm));
        } else
            ret = -ENOSYS;
    }

    return ret;
}

int proxy_write_playback_buffer(void *proxy_stream, void* buffer, int bytes)
{
    struct audio_proxy_stream *apstream = (struct audio_proxy_stream *)proxy_stream;
    int ret = 0, wrote = 0;

    /* Skip other sounds except AUX Digital Stream when AUX_DIGITAL is connected */
    if (apstream->stream_type != ASTREAM_PLAYBACK_AUX_DIGITAL &&
        getInstance()->active_playback_device == DEVICE_AUX_DIGITAL) {
        skip_pcm_processing(apstream, wrote);
        wrote = bytes;
        save_written_frames(apstream, wrote);
        return wrote;
     }

    if (apstream->stream_type == ASTREAM_PLAYBACK_COMPR_OFFLOAD) {
        if (apstream->compress) {
            if (apstream->ready_new_metadata) {
                compress_set_gapless_metadata(apstream->compress, &apstream->offload_metadata);
                ALOGI("%s-%s: sent gapless metadata(delay = %u, padding = %u) to Compress Device",
                       stream_table[apstream->stream_type], __func__,
                       apstream->offload_metadata.encoder_delay, apstream->offload_metadata.encoder_padding);
                apstream->ready_new_metadata = 0;
        }

            wrote = compress_write(apstream->compress, buffer, bytes);
            ALOGVV("%s-%s: wrote Request(%u bytes) to Compress Device, and Accepted (%u bytes)",
                    stream_table[apstream->stream_type], __func__, (unsigned int)bytes, wrote);
        }
    } else {
        if (apstream->pcm) {
            void *proc_buf_out = buffer;
            int dst_buffer_size = bytes;
#ifdef SEC_AUDIO_DUMP
            out_get_pcm_dump(apstream, proc_buf_out, dst_buffer_size, false);
#endif
            ret = pcm_write(apstream->pcm, (void *)proc_buf_out, (unsigned int)dst_buffer_size);
            if (ret == 0) {
                ALOGVV("%s-%s: writed %u bytes to PCM Device", stream_table[apstream->stream_type],
                                                               __func__, (unsigned int)bytes);

            } else {
                ALOGE("%s-%s: failed to write to PCM Device with %s",
                      stream_table[apstream->stream_type], __func__, pcm_get_error(apstream->pcm));
                skip_pcm_processing(apstream, wrote);
            }
            wrote = bytes;
            save_written_frames(apstream, wrote);
        }
    }

    return wrote;
}

int proxy_stop_playback_stream(void *proxy_stream)
{
    struct audio_proxy_stream *apstream = (struct audio_proxy_stream *)proxy_stream;
    int ret = 0;

    if (apstream->stream_type == ASTREAM_PLAYBACK_COMPR_OFFLOAD) {
        if (apstream->compress) {
            ret = compress_stop(apstream->compress);
            if (ret == 0)
                ALOGI("%s-%s: stopped Compress Device", stream_table[apstream->stream_type], __func__);
            else
                ALOGE("%s-%s: cannot stop Compress Offload(%s)", stream_table[apstream->stream_type],
                       __func__, compress_get_error(apstream->compress));

            apstream->ready_new_metadata = 1;
        }
    } else if (apstream->stream_type == ASTREAM_PLAYBACK_MMAP) {
        if (apstream->pcm) {
            ret = pcm_stop(apstream->pcm);
            if (ret == 0)
                ALOGI("%s-%s: stop MMAP Device", stream_table[apstream->stream_type], __func__);
            else
                ALOGE("%s-%s: cannot stop MMAP device with error(%s)", stream_table[apstream->stream_type],
                                               __func__, pcm_get_error(apstream->pcm));
        }
    }

    return ret;
}

int proxy_reconfig_playback_stream(void *proxy_stream, int type, void *config)
{
    struct audio_proxy_stream *apstream = (struct audio_proxy_stream *)proxy_stream;
    audio_stream_type new_type = (audio_stream_type)type;
    struct audio_config *new_config = (struct audio_config *)config;

    if (apstream) {
        apstream->stream_type = new_type;
        apstream->requested_sample_rate = new_config->sample_rate;
        apstream->requested_channel_mask = new_config->channel_mask;
        apstream->requested_format = new_config->format;

        return 0;
    } else
        return -1;
}

int proxy_get_render_position(void *proxy_stream, uint32_t *frames)
{
    struct audio_proxy *aproxy = getInstance();
    struct audio_proxy_stream *apstream = (struct audio_proxy_stream *)proxy_stream;
    uint32_t presented_frames = 0;

    unsigned long hw_frames;
    unsigned int sample_rate = 0;
    int ret = -ENODATA;

    if (frames != NULL) {
        *frames = 0;

        if (apstream->stream_type == ASTREAM_PLAYBACK_COMPR_OFFLOAD) {
            if (apstream->compress) {
                ret = compress_get_tstamp(apstream->compress, &hw_frames, &sample_rate);
                if (ret == 0) {
                    ALOGVV("%s-%s: rendered frames %u with sample_rate %u",
                           stream_table[apstream->stream_type], __func__, *frames, sample_rate);

                    presented_frames = (uint32_t)hw_frames;
                    *frames = presented_frames;
                } else
                    ret = -ENODATA;
            }
        }
    } else {
        ALOGE("%s-%s: Invalid Parameter with Null pointer parameter",
              stream_table[apstream->stream_type], __func__);
        ret =  -EINVAL;
    }

    return ret;
}

int proxy_get_presen_position(void *proxy_stream, uint64_t *frames, struct timespec *timestamp)
{
    struct audio_proxy *aproxy = getInstance();
    struct audio_proxy_stream *apstream = (struct audio_proxy_stream *)proxy_stream;
    uint64_t presented_frames = 0;

    unsigned long hw_frames;
    unsigned int sample_rate = 0;
    unsigned int avail = 0;
    int ret = -ENODATA;

    if (frames != NULL && timestamp != NULL) {
        *frames = 0;

        if (apstream->stream_type == ASTREAM_PLAYBACK_COMPR_OFFLOAD) {
            if (apstream->compress) {
                ret = compress_get_tstamp(apstream->compress, &hw_frames, &sample_rate);
                if (ret == 0) {
                    ALOGVV("%s-%s: presented frames %lu with sample_rate %u",
                       stream_table[apstream->stream_type], __func__, hw_frames, sample_rate);

                    presented_frames = (uint64_t)hw_frames;
                    *frames = presented_frames;

                    clock_gettime(CLOCK_MONOTONIC, timestamp);
                } else
                    ret = -ENODATA;
            }
        } else {
            if (apstream->pcm) {
                ret = pcm_get_htimestamp(apstream->pcm, &avail, timestamp);
                if (ret == 0) {
                    // Total Frame Count in kernel Buffer
                    uint64_t kernel_buffer_size = (uint64_t)apstream->pcmconfig.period_size *
                                                  (uint64_t)apstream->pcmconfig.period_count;

                    // Real frames which played out to device
                    int64_t signed_frames = apstream->frames - kernel_buffer_size + avail;

                    if (signed_frames >= 0) {
                        presented_frames = (uint64_t)signed_frames;
                        *frames = presented_frames;
                    } else {
                        ret = -ENODATA;
                    }
                } else {
                    ret = -ENODATA;
                }
            }
        }
    } else {
        ALOGE("%s-%s: Invalid Parameter with Null pointer parameter",
              stream_table[apstream->stream_type], __func__);
        ret =  -EINVAL;
    }

    return ret;
}

int proxy_getparam_playback_stream(void *proxy_stream, void *query_params, void *reply_params)
{
    struct audio_proxy_stream *apstream = (struct audio_proxy_stream *)proxy_stream;
    struct str_parms *query = (struct str_parms *)query_params;
    struct str_parms *reply = (struct str_parms *)reply_params;
    int val = -1;
    bool str_updated = false;
    struct audio_proxy *aproxy = getInstance();
    {
        /*
         * Supported Audio Configuration can be different as Target Project.
         * AudioHAL engineers have to modify these codes based on Target Project.
         */
        // supported audio formats
        if (str_parms_has_key(query, AUDIO_PARAMETER_STREAM_SUP_FORMATS)) {
            char formats_list[256];

            memset(formats_list, 0, 256);
            if (aproxy->is_dp_connected) {
                char * temp_format = formats_list;
                val = proxy_get_mixer_value_int((void *)aproxy, MIXER_CTL_DP_SUP_WIDTH);
                if (val > 0) {
                    str_updated = false;
                    for (int idx = 0; idx < MAX_DP_SUP_FORMAT_CNT; idx++) {
                        if (val & (0x1 << idx)) {
                            if (idx != 0 && str_updated) {
                                strncpy(temp_format, "|", 1);
                                temp_format++;
                            }
                            strncpy(temp_format, dp_supported_format_table[idx], strlen(dp_supported_format_table[idx]));
                            temp_format += (strlen(dp_supported_format_table[idx]));
                            str_updated = true;
                        }
                    }
                    ALOGI("proxy-%s: AUX_DIGITAL supported format-str: %s", __func__, formats_list);
                } else {
                    ALOGW("proxy-%s: unable to get AUX_DIGITAL supported format information", __func__);
                }
            } else {
                strncpy(formats_list, stream_format_table[apstream->stream_type],
                               strlen(stream_format_table[apstream->stream_type]));
            }
            str_parms_add_str(reply, AUDIO_PARAMETER_STREAM_SUP_FORMATS, formats_list);
        }

        // supported audio channel masks
        if (str_parms_has_key(query, AUDIO_PARAMETER_STREAM_SUP_CHANNELS)) {
            char channels_list[256];

            memset(channels_list, 0, 256);
            if (aproxy->is_dp_connected) {
                char * temp_channel = channels_list;
                val = proxy_get_mixer_value_int((void *)aproxy, MIXER_CTL_DP_SUP_CHANNEL);
                if (val > 0) {
                    str_updated = false;
                    for (int idx = 0; idx < MAX_DP_SUP_CHANNEL_CNT; idx++) {
                        if (val & (0x1 << idx)) {
                            if (idx != 0 && str_updated) {
                                strncpy(temp_channel, "|", 1);
                                temp_channel++;
                            }
                            strncpy(temp_channel, dp_supported_channel_table[idx], strlen(dp_supported_channel_table[idx]));
                            temp_channel += (strlen(dp_supported_channel_table[idx]));
                            str_updated = true;
                        }
                    }
                    ALOGI("proxy-%s: AUX_DIGITAL supported channel-str: %s", __func__, channels_list);
                } else {
                    ALOGW("proxy-%s: unable to get AUX_DIGITAL supported channel information", __func__);
                }
            } else {
                strncpy(channels_list, stream_channel_table[apstream->stream_type],
                                strlen(stream_channel_table[apstream->stream_type]));
            }
            str_parms_add_str(reply, AUDIO_PARAMETER_STREAM_SUP_CHANNELS, channels_list);
        }

        // supported audio samspling rates
        if (str_parms_has_key(query, AUDIO_PARAMETER_STREAM_SUP_SAMPLING_RATES)) {
            char rates_list[256];

            memset(rates_list, 0, 256);
            if (aproxy->is_dp_connected) {
                char * temp_rate = rates_list;
                val = proxy_get_mixer_value_int((void *)aproxy, MIXER_CTL_DP_SUP_SAMPLERATE);
                if (val > 0) {
                    str_updated = false;
                    for (int idx = 0; idx < MAX_DP_SUP_RATE_CNT; idx++) {
                        if (val & (0x1 << idx)) {
                            if (idx != 0 && str_updated) {
                                strncpy(temp_rate, "|", 1);
                                temp_rate++;
                            }
                            strncpy(temp_rate, dp_supported_rate_table[idx], strlen(dp_supported_rate_table[idx]));
                            temp_rate += (strlen(dp_supported_rate_table[idx]));
                            str_updated = true;
                        }
                    }
                    ALOGI("proxy-%s: AUX_DIGITAL supported Rate-str: %s", __func__, rates_list);
                } else {
                    ALOGW("proxy-%s: unable to get AUX_DIGITAL supported Rate information", __func__);
                }
            } else {
                strncpy(rates_list, stream_rate_table[apstream->stream_type],
                             strlen(stream_rate_table[apstream->stream_type]));
            }
            str_parms_add_str(reply, AUDIO_PARAMETER_STREAM_SUP_SAMPLING_RATES, rates_list);
        }
    }

    return 0;
}

int proxy_setparam_playback_stream(void *proxy_stream, void *parameters)
{
    struct audio_proxy_stream *apstream = (struct audio_proxy_stream *)proxy_stream;
    struct str_parms *parms = (struct str_parms *)parameters;

    char value[32];
    int ret = 0;

    if (apstream->stream_type == ASTREAM_PLAYBACK_COMPR_OFFLOAD) {
        struct compr_gapless_mdata tmp_mdata;
        bool need_to_set_metadata = false;

        tmp_mdata.encoder_delay = 0;
        tmp_mdata.encoder_padding = 0;

        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_DELAY_SAMPLES, value, sizeof(value));
        if (ret >= 0) {
            tmp_mdata.encoder_delay = atoi(value);
            ALOGI("%s-%s: Codec Delay Samples(%u)", stream_table[apstream->stream_type], __func__,
                                                    tmp_mdata.encoder_delay);
            need_to_set_metadata = true;
        }

        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_PADDING_SAMPLES, value, sizeof(value));
        if (ret >= 0) {
            tmp_mdata.encoder_padding = atoi(value);
            ALOGI("%s-%s: Codec Padding Samples(%u)", stream_table[apstream->stream_type], __func__,
                                                      tmp_mdata.encoder_padding);
            need_to_set_metadata = true;
        }

        if (need_to_set_metadata) {
            apstream->offload_metadata = tmp_mdata;
            apstream->ready_new_metadata = 1;
        }
    }

    return ret;
}

uint32_t proxy_get_playback_latency(void *proxy_stream)
{
    struct audio_proxy_stream *apstream = (struct audio_proxy_stream *)proxy_stream;
    uint32_t latency;

    // Total Latency = ALSA Buffer latency + HW Latency
    if (apstream->stream_type == ASTREAM_PLAYBACK_COMPR_OFFLOAD) {
        /*
         * Offload HW latency
         * - A-Box firmware triggers offload play once two buffers of 20msec each are decoded - 20msec
         * - Post processing of decoded data - 10 msec
         * - 20msec extra is provided considering scheduling delays
         * therefore total offload HW latency will 50msec
         */
        latency = 50;
    } else {
        latency = (apstream->pcmconfig.period_count * apstream->pcmconfig.period_size * 1000) / (apstream->pcmconfig.rate);
        latency += 0;   // Need to check HW Latency
    }

    return latency;
}

// select best pcmconfig among requested two configs
bool proxy_select_best_playback_pcmconfig(
    void *proxy __unused,
    void *cur_proxy_stream __unused,
    int compr_upscaler __unused)
{
    return false;
}

/* selecting best playback pcm config to configure USB device */
void proxy_set_best_playback_pcmconfig(
    void *proxy __unused,
    void *proxy_stream __unused)
{
    return;
}

/* reset playback pcm config for USB device default */
void proxy_reset_playback_pcmconfig(void *proxy __unused)
{
    return;
}

void proxy_dump_playback_stream(void *proxy_stream, int fd)
{
    struct audio_proxy_stream *apstream = (struct audio_proxy_stream *)proxy_stream;

    const size_t len = 256;
    char buffer[len];

    if (apstream->pcm != NULL) {
        snprintf(buffer, len, "\toutput pcm config sample rate: %d\n",apstream->pcmconfig.rate);
        write(fd,buffer,strlen(buffer));
        snprintf(buffer, len, "\toutput pcm config period size : %d\n",apstream->pcmconfig.period_size);
        write(fd,buffer,strlen(buffer));
        snprintf(buffer, len, "\toutput pcm config format: %d\n",apstream->pcmconfig.format);
        write(fd,buffer,strlen(buffer));
    }

    if (apstream->compress != NULL) {
        if (apstream->comprconfig.codec != NULL) {
            snprintf(buffer, len, "\toutput offload codec id: %d\n",apstream->comprconfig.codec->id);
            write(fd,buffer,strlen(buffer));
            snprintf(buffer, len, "\toutput offload codec input channel: %d\n",apstream->comprconfig.codec->ch_in);
            write(fd,buffer,strlen(buffer));
            snprintf(buffer, len, "\toutput offload codec output channel: %d\n",apstream->comprconfig.codec->ch_out);
            write(fd,buffer,strlen(buffer));
            snprintf(buffer, len, "\toutput offload sample rate: %d\n",apstream->comprconfig.codec->sample_rate);
            write(fd,buffer,strlen(buffer));
            snprintf(buffer, len, "\toutput offload bit rate : %d\n",apstream->comprconfig.codec->bit_rate);
            write(fd,buffer,strlen(buffer));
            snprintf(buffer, len, "\toutput offload config format: %d\n",apstream->comprconfig.codec->format);
            write(fd,buffer,strlen(buffer));
        }

        snprintf(buffer, len, "\tOffload Fragment Size: %d\n",apstream->comprconfig.fragment_size);
        write(fd,buffer,strlen(buffer));
        snprintf(buffer, len, "\tOffload Fragments: %d\n",apstream->comprconfig.fragments);
        write(fd,buffer,strlen(buffer));
    }

    return ;
}


void *proxy_create_capture_stream(void *proxy, int type, int usage, void *config, char *address __unused)
{
    struct audio_proxy *aproxy = proxy;
    audio_stream_type stream_type = (audio_stream_type)type;
    audio_usage       stream_usage = (audio_usage)usage;
    struct audio_config *requested_config = (struct audio_config *)config;

    struct audio_proxy_stream *apstream;

    apstream = (struct audio_proxy_stream *)calloc(1, sizeof(struct audio_proxy_stream));
    if (!apstream) {
        ALOGE("proxy-%s: failed to allocate memory for Proxy Stream", __func__);
        return NULL;;
    }

    /* Stores the requested configurationss */
    apstream->requested_sample_rate = requested_config->sample_rate;
    apstream->requested_channel_mask = requested_config->channel_mask;
    apstream->requested_format = requested_config->format;

    apstream->stream_type = stream_type;
    apstream->stream_usage = stream_usage;

    // Initialize Post-Processing
    apstream->need_channelconversion = false;
    apstream->need_resampling = false;

    apstream->actual_read_buf = NULL;
    apstream->actual_read_buf_size = 0;

    apstream->proc_buf_out = NULL;
    apstream->proc_buf_size = 0;

    apstream->resampler = NULL;

    apstream->need_update_pcm_config = false;
    apstream->skip_ch_convert = false;
#ifdef SEC_AUDIO_SAMSUNGRECORD
    apstream->skip_format_convert = false;
#endif
#ifdef SEC_AUDIO_RESAMPLER
    apstream->conversion_buffer_size = 0;
#endif

    /* Sets basic configuration from Stream Type. */
    switch (apstream->stream_type) {
        // For VTS
        case ASTREAM_CAPTURE_NO_ATTRIBUTE:
            apstream->sound_card = PRIMARY_CAPTURE_CARD;
            apstream->sound_device = PRIMARY_CAPTURE_DEVICE;
            apstream->pcmconfig = pcm_config_primary_capture;

            break;

        case ASTREAM_CAPTURE_PRIMARY:
            apstream->sound_card = PRIMARY_CAPTURE_CARD;
            apstream->sound_device = get_pcm_device_number(aproxy, apstream);
            apstream->pcmconfig = pcm_config_primary_capture;

            update_capture_pcmconfig(apstream);

            /*
            ** Reset previous configurations and release resampler if running
            ** for reconfiguration purpose
            */
            apstream->need_channelconversion = false;

            check_conversion(apstream);
            break;

        case ASTREAM_CAPTURE_CALL:
            apstream->sound_card = CALL_RECORD_CARD;
            apstream->sound_device = get_pcm_device_number(aproxy, apstream);
            apstream->pcmconfig = pcm_config_call_record;

            check_conversion(apstream);
            break;

        case ASTREAM_CAPTURE_LOW_LATENCY:
            apstream->sound_card = LOW_CAPTURE_CARD;
            apstream->sound_device = get_pcm_device_number(aproxy, apstream);
            apstream->pcmconfig = pcm_config_low_capture;

            update_capture_pcmconfig(apstream);
            check_conversion(apstream);
            break;

        case ASTREAM_CAPTURE_MMAP:
            apstream->sound_card = MMAP_CAPTURE_CARD;
            apstream->sound_device = get_pcm_device_number(aproxy, apstream);
            apstream->pcmconfig = pcm_config_mmap_capture;

            /* update HW PCM configuration with requested config, as MMAP usage cann't
                use software conversions for sample rate and channels, format is fixed to
                16bit */
            if (apstream->requested_sample_rate != apstream->pcmconfig.rate) {
                apstream->pcmconfig.rate = apstream->requested_sample_rate;
                // Adjust period_size according to sample rate
                apstream->pcmconfig.period_size = (apstream->pcmconfig.rate * PREDEFINED_MMAP_CAPTURE_DURATION) / 1000;

                // WDMA in A-Box is 128-bit aligned, so period_size has to be multiple of 4 frames
                apstream->pcmconfig.period_size &= 0xFFFFFFFC;
                ALOGD("%s-%s: updates samplig rate to %u, period_size to %u",
                    stream_table[apstream->stream_type], __func__,
                    apstream->pcmconfig.rate, apstream->pcmconfig.period_size);
            }

            if (audio_channel_count_from_in_mask(apstream->requested_channel_mask)
                != apstream->pcmconfig.channels) {
                apstream->pcmconfig.channels = audio_channel_count_from_in_mask(apstream->requested_channel_mask);
                ALOGD("%s-%s: updates channel count to %u", stream_table[apstream->stream_type],
                                                            __func__, apstream->pcmconfig.channels);
            }
            break;

        case ASTREAM_CAPTURE_FM_TUNER:
        case ASTREAM_CAPTURE_FM_RECORDING:
            apstream->sound_card = FM_RECORD_CARD;
            apstream->sound_device = get_pcm_device_number(aproxy, apstream);
            apstream->pcmconfig = pcm_config_fm_record;

            check_conversion(apstream);
            break;

        default:
            ALOGE("proxy-%s: failed to open Proxy Stream as unknown stream type(%d)", __func__,
                                                                          apstream->stream_type);
            goto err_open;
    }

    apstream->pcm = NULL;
    apstream->compress = NULL;

#ifdef SEC_AUDIO_DUMP
    if(aproxy->input_cnt >= INT_MAX)
        aproxy->input_cnt = 0;
    apstream->input_count = ++aproxy->input_cnt;
#endif

    ALOGI("proxy-%s: opened Proxy Stream(%s)", __func__, stream_table[apstream->stream_type]);
    return (void *)apstream;

err_open:
    free(apstream);
    return NULL;
}

void proxy_destroy_capture_stream(void *proxy_stream)
{
    struct audio_proxy_stream *apstream = (struct audio_proxy_stream *)proxy_stream;

    if (apstream) {
        if (apstream->resampler) {
            ALOGV("%s-%s: released resampler", stream_table[apstream->stream_type], __func__);
#ifdef SEC_AUDIO_RESAMPLER
            PostProcessConvertorClear(apstream->resampler);
#else
            release_resampler(apstream->resampler);
#endif
        }

        if (apstream->actual_read_buf)
            free(apstream->actual_read_buf);

        if (apstream->proc_buf_out)
            free(apstream->proc_buf_out);

#ifdef SEC_AUDIO_RESAMPLER
        if (apstream->conversion_buffer)
            free(apstream->conversion_buffer);
#endif

#ifdef SEC_AUDIO_DUMP
        in_stop_pcm_dump(apstream);
#endif

        free(apstream);
    }

    return ;
}

int proxy_close_capture_stream(void *proxy_stream)
{
    struct audio_proxy_stream *apstream = (struct audio_proxy_stream *)proxy_stream;
    struct audio_proxy *aproxy = getInstance();
    int ret = 0;

    /* Close Normal PCM Device */
    if (apstream->pcm) {
        ret = pcm_close(apstream->pcm);
        apstream->pcm = NULL;
        ALOGI("%s-%s: closed PCM Device", stream_table[apstream->stream_type], __func__);
    }
    if (apstream->dma_pcm) {
        pcm_close(apstream->dma_pcm);
        apstream->dma_pcm = NULL;
    }

    /* reset pcmconfig to primary configuration
     * if current device is usb-headset-mic pcmconfig might be changed */
    if (apstream->stream_type == ASTREAM_CAPTURE_PRIMARY) {
       apstream->pcmconfig = pcm_config_primary_capture;
       apstream->need_channelconversion = false;
       check_conversion(apstream);
    }
    ALOGI("%s-%s: closed PCM Device", stream_table[apstream->stream_type], __func__);

    return ret;
}

int proxy_open_capture_stream(void *proxy_stream, int32_t min_size_frames, void *mmap_info)
{
    struct audio_proxy_stream *apstream = (struct audio_proxy_stream *)proxy_stream;
    struct audio_proxy *aproxy = getInstance();
    struct audio_mmap_buffer_info *info = (struct audio_mmap_buffer_info *)mmap_info;
    unsigned int sound_card;
    unsigned int sound_device;
    unsigned int flags;
    int ret = 0;
    char pcm_path[MAX_PCM_PATH_LEN];

    if (is_active_usage_APCall(aproxy) && apstream->pcmconfig.rate != 48000) {
        apstream->sound_card = PRIMARY_CAPTURE_CARD;
        apstream->sound_device = get_pcm_device_number(aproxy, apstream);
        apstream->pcmconfig = pcm_config_primary_capture;

        check_conversion(apstream);
    }

    /* Get PCM Device */
    sound_card = apstream->sound_card;
    sound_device = apstream->sound_device;

    /* Open Normal PCM Device */
    if (apstream->pcm == NULL) {
        if (apstream->stream_type == ASTREAM_CAPTURE_MMAP) {
            flags = PCM_IN | PCM_MMAP | PCM_NOIRQ | PCM_MONOTONIC;

            adjust_mmap_period_count(apstream, &apstream->pcmconfig, min_size_frames);
        } else
            flags = PCM_IN | PCM_MONOTONIC;

        /* open WDMA pcm first to trigger DMA */
        apstream->dma_pcm = pcm_open(sound_card, sound_device, flags, &apstream->pcmconfig);
        if (apstream->dma_pcm && !pcm_is_ready(apstream->dma_pcm)) {
            /* pcm_open does always return pcm structure, not NULL */
            ALOGE("%s-%s: PCM Device is not ready with Sampling_Rate(%u) error(%s)!",
                  stream_table[apstream->stream_type], __func__, apstream->pcmconfig.rate,
                  pcm_get_error(apstream->dma_pcm));
            goto err_open;
        }

        snprintf(pcm_path, sizeof(pcm_path), "/dev/snd/pcmC%uD%u%c", sound_card, sound_device, 'c');

        ALOGI("%s-%s: The opened PCM Device is %s with Sampling_Rate(%u) PCM_Format(%d) Channel(%d)",
              stream_table[apstream->stream_type], __func__, pcm_path,
              apstream->pcmconfig.rate, apstream->pcmconfig.format, apstream->pcmconfig.channels);
        apstream->pcm = apstream->dma_pcm;
        apstream->dma_pcm = NULL;

        apstream->compress = NULL;

        if (apstream->stream_type == ASTREAM_CAPTURE_MMAP) {
            unsigned int offset1 = 0;
            unsigned int frames1 = 0;
            unsigned int buf_size = 0;
            unsigned int mmap_size = 0;

            ret = pcm_mmap_begin(apstream->pcm, &info->shared_memory_address, &offset1, &frames1);
            if (ret == 0)  {
                ALOGI("%s-%s: PCM Device begin MMAP", stream_table[apstream->stream_type], __func__);

                info->buffer_size_frames = pcm_get_buffer_size(apstream->pcm);
                buf_size = pcm_frames_to_bytes(apstream->pcm, info->buffer_size_frames);
                info->burst_size_frames = apstream->pcmconfig.period_size;
                // get mmap buffer fd
                ret = get_mmap_data_fd(proxy_stream, AUSAGE_CAPTURE,
                                                        &info->shared_memory_fd, &mmap_size);
                if (ret < 0) {
                    // Fall back to poll_fd mode, shared mode
                    info->shared_memory_fd = pcm_get_poll_fd(apstream->pcm);
                    ALOGI("%s-%s: PCM Device MMAP Exclusive mode not support",
                        stream_table[apstream->stream_type], __func__);
                } else {
                    if (mmap_size < buf_size) {
                        ALOGE("%s-%s: PCM Device MMAP buffer size not matching",
                              stream_table[apstream->stream_type], __func__);
                        goto err_open;
                    }
                    // FIXME: indicate exclusive mode support by returning a negative buffer size
                    info->buffer_size_frames *= -1;
                }

                memset(info->shared_memory_address, 0,
                       pcm_frames_to_bytes(apstream->pcm, info->buffer_size_frames));

                ret = pcm_mmap_commit(apstream->pcm, 0, MMAP_PERIOD_SIZE);
                if (ret < 0) {
                    ALOGE("%s-%s: PCM Device cannot commit MMAP with error(%s)",
                          stream_table[apstream->stream_type], __func__, pcm_get_error(apstream->pcm));
                    goto err_open;
                } else {
                    ALOGI("%s-%s: PCM Device commit MMAP", stream_table[apstream->stream_type], __func__);
                    ret = 0;
                }
            } else {
                ALOGE("%s-%s: PCM Device cannot begin MMAP with error(%s)",
                      stream_table[apstream->stream_type], __func__, pcm_get_error(apstream->pcm));
                goto err_open;
            }
        }
    } else
        ALOGW("%s-%s: PCM Device is already opened!", stream_table[apstream->stream_type], __func__);

    apstream->need_update_pcm_config = false;

    return ret;

err_open:
    proxy_close_capture_stream(proxy_stream);
    return -ENODEV;
}

int proxy_start_capture_stream(void *proxy_stream)
{
    struct audio_proxy_stream *apstream = (struct audio_proxy_stream *)proxy_stream;
    int ret = 0;

    // In case of PCM Playback, pcm_start call is not needed as auto-start
    if (apstream->pcm) {
        ret = pcm_start(apstream->pcm);
        if (ret == 0)
            ALOGI("%s-%s: started PCM Device", stream_table[apstream->stream_type], __func__);
        else
            ALOGE("%s-%s: cannot start PCM(%s)", stream_table[apstream->stream_type], __func__,
                                                 pcm_get_error(apstream->pcm));
    }

    return ret;
}

int proxy_read_capture_buffer(void *proxy_stream, void *buffer, int bytes)
{
    struct audio_proxy_stream *apstream = (struct audio_proxy_stream *)proxy_stream;
    int frames_request = bytes / proxy_get_requested_frame_size(apstream);
    int frames_actual = -1;
#ifdef SEC_AUDIO_SAMSUNGRECORD
    int channel_cnt = apstream->skip_ch_convert ? proxy_get_actual_channel_count(apstream)
                        : audio_channel_count_from_in_mask(apstream->requested_channel_mask);
    int format_cnt = audio_bytes_per_sample(proxy_get_last_format(apstream));

    if (apstream->skip_ch_convert || apstream->skip_format_convert) {
        frames_request = bytes / (channel_cnt * format_cnt);
    }
#endif

    {
        frames_actual = read_and_process_frames(apstream, buffer, frames_request);
        ALOGVV("%s-%s: requested read frames = %d vs. actual processed read frames = %d",
               stream_table[apstream->stream_type], __func__, frames_request, frames_actual);
    }

    if (frames_actual < 0) {
        return frames_actual;
    } else {
        /* Saves read frames to calcurate timestamp */
        apstream->frames += frames_actual;
        ALOGVV("%s-%s: cumulative read = %u frames", stream_table[apstream->stream_type], __func__,
                                          (unsigned int)apstream->frames);
        return bytes;
    }
}

int proxy_stop_capture_stream(void *proxy_stream)
{
    struct audio_proxy_stream *apstream = (struct audio_proxy_stream *)proxy_stream;
    int ret = 0;

    if (apstream->pcm) {
        ret = pcm_stop(apstream->pcm);
        if (ret == 0)
            ALOGI("%s-%s: stopped PCM Device", stream_table[apstream->stream_type], __func__);
        else
            ALOGE("%s-%s: cannot stop PCM(%s)", stream_table[apstream->stream_type], __func__,
                                                pcm_get_error(apstream->pcm));
    }

    return ret;
}

int proxy_reconfig_capture_stream(void *proxy_stream, int type, void *config)
{
    struct audio_proxy_stream *apstream = (struct audio_proxy_stream *)proxy_stream;
    audio_stream_type new_type = (audio_stream_type)type;
    struct audio_config *new_config = (struct audio_config *)config;

    if (apstream) {
        apstream->stream_type = new_type;
        apstream->requested_sample_rate = new_config->sample_rate;
        apstream->requested_channel_mask = new_config->channel_mask;
        apstream->requested_format = new_config->format;

        // If some stream types need to be reset, it has to reconfigure conversions

        return 0;
    } else
        return -1;
}

int proxy_reconfig_capture_usage(void *proxy_stream, int type, int usage)
{
    struct audio_proxy_stream *apstream = (struct audio_proxy_stream *)proxy_stream;

    if (apstream == NULL)
        return -1;

    struct audio_proxy *aproxy = getInstance();
    audio_stream_type stream_type = (audio_stream_type)type;
    audio_usage       stream_usage = (audio_usage)usage;

    if (stream_usage != AUSAGE_NONE)
        apstream->stream_usage = stream_usage;

     switch (stream_type) {
        case ASTREAM_CAPTURE_PRIMARY:
            apstream->stream_type = stream_type;
            apstream->sound_card = PRIMARY_CAPTURE_CARD;
            apstream->sound_device = get_pcm_device_number(aproxy, apstream);

            apstream->pcmconfig = pcm_config_primary_capture;

            update_capture_pcmconfig(apstream);
            /*
            ** Reset previous configurations and release resampler if running
            ** for reconfiguration purpose
            */
            apstream->need_channelconversion = false;

            check_conversion(apstream);
            break;

        case ASTREAM_CAPTURE_CALL:
            apstream->stream_type = stream_type;
            apstream->sound_card = CALL_RECORD_CARD;
            apstream->sound_device = get_pcm_device_number(aproxy, apstream);
            apstream->pcmconfig = pcm_config_call_record;

            check_conversion(apstream);
            break;
        default:
            ALOGE("proxy-%s: failed to reconfig Proxy Stream as unknown stream type(%d)", __func__, stream_type);
            return -1;
    }

    ALOGI("proxy-%s: reconfig Proxy Stream(%s)", __func__, stream_table[apstream->stream_type]);

    return 0;
}

int proxy_get_capture_pos(void *proxy_stream, int64_t *frames, int64_t *time)
{
    struct audio_proxy_stream *apstream = (struct audio_proxy_stream *)proxy_stream;
    unsigned int avail = 0;
    struct timespec timestamp;
    int ret = -ENOSYS;;

    if (frames != NULL && time != NULL) {
        *frames = 0;
        *time = 0;

        if (apstream->pcm) {
            ret = pcm_get_htimestamp(apstream->pcm, &avail, &timestamp);
            if (ret == 0) {
                // Real frames which captured in from device
                *frames = apstream->frames + avail;
                // Nano Seconds Unit Time
                *time = timestamp.tv_sec * 1000000000LL + timestamp.tv_nsec;
                ret = 0;
            }
        }
    } else {
        ALOGE("%s-%s: Invalid Parameter with Null pointer parameter",
              stream_table[apstream->stream_type], __func__);
        ret =  -EINVAL;
    }

    return ret;
}

int proxy_get_active_microphones(void *proxy_stream, void *array, int *count)
{
    struct audio_proxy *aproxy = getInstance();
    struct audio_proxy_stream *apstream = (struct audio_proxy_stream *)proxy_stream;
    struct audio_microphone_characteristic_t *mic_array = array;
    size_t *mic_count = (size_t *)count;
    size_t actual_mic_count = 0;
    int ret = 0;

    if (apstream) {
        if (apstream->stream_type == ASTREAM_CAPTURE_NO_ATTRIBUTE ||
            apstream->stream_type == ASTREAM_CAPTURE_PRIMARY ||
            apstream->stream_type == ASTREAM_CAPTURE_LOW_LATENCY ||
            apstream->stream_type == ASTREAM_CAPTURE_MMAP) {
            device_type active_device = aproxy->active_capture_device;
            if (active_device == DEVICE_NONE) {
                ALOGE("%s-%s: There are no active MIC", stream_table[apstream->stream_type], __func__);
                ret = -ENOSYS;
            }

            if (*mic_count == 0) {
                if (active_device == DEVICE_STEREO_MIC)
                    actual_mic_count = 2;
                else
                    actual_mic_count = 1;
                ALOGI("proxy-%s: requested number of microphone, return %zu", __func__, *mic_count);
            } else {
                if (active_device == DEVICE_STEREO_MIC) {
                    for (int i = 0; i < 2; i++) {
                        mic_array[i] = aproxy->mic_info[i];
                        ALOGD("%s-%s: %dth MIC = %s", stream_table[apstream->stream_type], __func__,
                                                      i+1, mic_array[i].device_id);
                        actual_mic_count++;
                    }
                } else if (active_device == DEVICE_MAIN_MIC) {
                        mic_array[0] = aproxy->mic_info[0];
                        ALOGD("%s-%s: Active MIC = %s", stream_table[apstream->stream_type], __func__,
                                                        mic_array[0].device_id);
                        actual_mic_count = 1;
                } else if (active_device == DEVICE_SUB_MIC) {
                        mic_array[0] = aproxy->mic_info[1];
                        ALOGD("%s-%s: Active MIC = %s", stream_table[apstream->stream_type], __func__,
                                                        mic_array[0].device_id);
                        actual_mic_count = 1;
                } else {
                    ALOGE("%s-%s: Abnormal active device(%s)", stream_table[apstream->stream_type],
                                                               __func__, device_table[active_device]);
                    ret = -ENOSYS;
                }
            }
        } else {
            ALOGE("%s-%s: This stream doesn't have active MIC", stream_table[apstream->stream_type],
                                                                __func__);
            ret = -ENOSYS;
        }
    } else {
        ALOGE("proxy-%s: apstream is NULL", __func__);
        ret = -ENOSYS;
    }

    *mic_count = actual_mic_count;

    return ret;
}

int proxy_getparam_capture_stream(void *proxy_stream, void *query_params, void *reply_params)
{
    struct audio_proxy_stream *apstream = (struct audio_proxy_stream *)proxy_stream;

    struct str_parms *query = (struct str_parms *)query_params;
    struct str_parms *reply = (struct str_parms *)reply_params;

    {
        /*
         * Supported Audio Configuration can be different as Target Project.
         * AudioHAL engineers have to modify these codes based on Target Project.
         */
        // supported audio formats
        if (str_parms_has_key(query, AUDIO_PARAMETER_STREAM_SUP_FORMATS)) {
            char formats_list[256];

            memset(formats_list, 0, 256);
            strncpy(formats_list, stream_format_table[apstream->stream_type],
                           strlen(stream_format_table[apstream->stream_type]));
            str_parms_add_str(reply, AUDIO_PARAMETER_STREAM_SUP_FORMATS, formats_list);
        }

        // supported audio channel masks
        if (str_parms_has_key(query, AUDIO_PARAMETER_STREAM_SUP_CHANNELS)) {
            char channels_list[256];

            memset(channels_list, 0, 256);
            strncpy(channels_list, stream_channel_table[apstream->stream_type],
                            strlen(stream_channel_table[apstream->stream_type]));
            str_parms_add_str(reply, AUDIO_PARAMETER_STREAM_SUP_CHANNELS, channels_list);
        }

        // supported audio samspling rates
        if (str_parms_has_key(query, AUDIO_PARAMETER_STREAM_SUP_SAMPLING_RATES)) {
            char rates_list[256];

            memset(rates_list, 0, 256);
            strncpy(rates_list, stream_rate_table[apstream->stream_type],
                         strlen(stream_rate_table[apstream->stream_type]));
            str_parms_add_str(reply, AUDIO_PARAMETER_STREAM_SUP_SAMPLING_RATES, rates_list);
        }
    }

    return 0;
}

int proxy_setparam_capture_stream(
    void *proxy_stream __unused,
    void *parameters __unused)
{
    return 0;
}

void proxy_dump_capture_stream(void *proxy_stream, int fd)
{
    struct audio_proxy_stream *apstream = (struct audio_proxy_stream *)proxy_stream;

    const size_t len = 256;
    char buffer[len];

    if (apstream->pcm != NULL) {
        snprintf(buffer, len, "\tinput pcm config sample rate: %d\n",apstream->pcmconfig.rate);
        write(fd,buffer,strlen(buffer));
        snprintf(buffer, len, "\tinput pcm config period size : %d\n",apstream->pcmconfig.period_size);
        write(fd,buffer,strlen(buffer));
        snprintf(buffer, len, "\tinput pcm config format: %d\n",apstream->pcmconfig.format);
        write(fd,buffer,strlen(buffer));
    }

    return ;
}

void proxy_update_capture_usage(void *proxy_stream, int usage)
{
    struct audio_proxy_stream *apstream = (struct audio_proxy_stream *)proxy_stream;
    audio_usage    stream_usage = (audio_usage)usage;

    if(apstream) {
        apstream->stream_usage = stream_usage;
        ALOGD("proxy-%s: apstream->stream_usage = %d", __func__, apstream->stream_usage);
    } else {
        ALOGD("proxy-%s: apstream is NULL", __func__);
    }
    return ;
}

int proxy_get_mmap_position(void *proxy_stream, void *pos)
{
    struct audio_proxy_stream *apstream = (struct audio_proxy_stream *)proxy_stream;
    struct audio_mmap_position *position = (struct audio_mmap_position *)pos;
    int ret = -ENOSYS;

    if ((apstream->stream_type == ASTREAM_PLAYBACK_MMAP || apstream->stream_type == ASTREAM_CAPTURE_MMAP)&&
         apstream->pcm) {
        struct timespec ts = { 0, 0 };

        ret = pcm_mmap_get_hw_ptr(apstream->pcm, (unsigned int *)&position->position_frames, &ts);
        if (ret < 0) {
            ALOGE("proxy-%s: get_hw_ptr error %s ", __func__, pcm_get_error(apstream->pcm));
        } else if (ret == 0) {
             position->time_nanoseconds = audio_utils_ns_from_timespec(&ts);
        }
    }

    return ret;
}


/******************************************************************************/
/**                                                                          **/
/** Interfaces for Audio Device Proxy                                        **/
/**                                                                          **/
/******************************************************************************/

/*
 *  Route Control Functions
 */
bool proxy_init_route(void *proxy, char *path)
{
    struct audio_proxy *aproxy = proxy;
    struct audio_route *ar = NULL;
    bool ret = false;

    if (aproxy) {
        aproxy->mixer = mixer_open(MIXER_CARD0);
        proxy_set_mixercontrol(aproxy, TICKLE_CONTROL, ABOX_TICKLE_ON);
        if (aproxy->mixer) {
            // In order to get add event, subscription has to be here!
            mixer_subscribe_events(aproxy->mixer, 1);

            ar = audio_route_init(MIXER_CARD0, path);
            if (!ar) {
                ALOGE("proxy-%s: failed to init audio route", __func__);
                mixer_subscribe_events(aproxy->mixer, 0);
                mixer_close(aproxy->mixer);
                aproxy->mixer = NULL;
            } else {
                aproxy->aroute = ar;
                aproxy->xml_path = strdup(path);    // Save Mixer Paths XML File path

                aproxy->active_playback_ausage   = AUSAGE_NONE;
                aproxy->active_playback_device   = DEVICE_NONE;
                aproxy->active_playback_modifier = MODIFIER_NONE;

                aproxy->active_capture_ausage   = AUSAGE_NONE;
                aproxy->active_capture_device   = DEVICE_NONE;
                aproxy->active_capture_modifier = MODIFIER_NONE;

                ALOGI("proxy-%s: opened Mixer & initialized audio route", __func__);
                ret = true;

                /* Create Mixer Control Update Thread */
                pthread_rwlock_init(&aproxy->mixer_update_lock, NULL);

                if (audio_route_missing_ctl(ar)) {
                    pthread_create(&aproxy->mixer_update_thread, NULL, mixer_update_loop, aproxy);
                    ALOGI("proxy-%s: missing control found, update thread is created", __func__);
                } else
                    mixer_subscribe_events(aproxy->mixer, 0);
            }
        } else
            ALOGE("proxy-%s: failed to open Mixer", __func__);
    }

    return ret;
}

void proxy_deinit_route(void *proxy)
{
    struct audio_proxy *aproxy = proxy;

    if (aproxy) {
        pthread_rwlock_wrlock(&aproxy->mixer_update_lock);

        if (aproxy->aroute) {
            audio_route_free(aproxy->aroute);
            aproxy->aroute = NULL;
        }
        if (aproxy->mixer) {
            mixer_close(aproxy->mixer);
            aproxy->mixer = NULL;
        }

        pthread_rwlock_unlock(&aproxy->mixer_update_lock);
        pthread_rwlock_destroy(&aproxy->mixer_update_lock);
        free(aproxy->xml_path);
    }
    ALOGI("proxy-%s: closed Mixer & deinitialized audio route", __func__);

    return ;
}

bool proxy_update_route(void *proxy, int ausage, int device)
{
    struct audio_proxy *aproxy = proxy;
    audio_usage __unused routed_ausage = (audio_usage)ausage;
    device_type __unused routed_device = (device_type)device;

    // Temp
    if (aproxy != NULL) {
        routed_ausage = AUSAGE_NONE;
        routed_device = DEVICE_NONE;
    }

    return true;
}

bool proxy_set_route(void *proxy, int ausage, int device, int modifier, bool set)
{
    struct audio_proxy *aproxy = proxy;

    audio_usage   routed_ausage = (audio_usage)ausage;
    device_type   routed_device = (device_type)device;
    audio_usage   active_ausage = AUSAGE_NONE;
    device_type   active_device = DEVICE_NONE;

    modifier_type routed_modifier = (modifier_type)modifier;

    if (set) {
        /* check whether path routing is for AP/CP call bandwidth or speaker/DEX Device Change */
        if (routed_device < DEVICE_MAIN_MIC) {
            active_ausage = aproxy->active_playback_ausage;
            active_device = aproxy->active_playback_device;
        } else {
            active_ausage = aproxy->active_capture_ausage;
            active_device = aproxy->active_capture_device;
        }

        if (is_usage_Call(active_ausage) &&
                is_usage_Call(routed_ausage)) {
            /* check whether internal path nodes close/re-open should be skipped,
             * for following scenarios
             * - cp call bandwidth change
             * - Dex speaker device state change during ap/cp call
            */
            if (((active_ausage != routed_ausage) && (active_device == routed_device) &&
                (is_usage_CPCall(active_ausage) && is_usage_CPCall(routed_ausage))) ||
                ((active_ausage == routed_ausage) && (active_device != routed_device) &&
                is_device_speaker(routed_device) && is_device_speaker(active_device))) {
                ALOGI("proxy-%s: skip output path loopback PCMs re-open",
                    __func__);
                ALOGI("proxy-%s: active-device(%s) requested-device(%s)", __func__,
                    device_table[active_device],
                    device_table[routed_device]);
                aproxy->skip_internalpath = true;
            }
        }

        if (routed_device < DEVICE_MAIN_MIC) {
            /* Do Specific Operation based on Audio Path */
            do_operations_by_playback_route_set(aproxy, routed_ausage, routed_device);

            if (aproxy->active_playback_ausage != AUSAGE_NONE &&
                aproxy->active_playback_device != DEVICE_NONE) {
                disable_internal_path(aproxy, aproxy->active_playback_ausage,
                                        aproxy->active_playback_device);
                set_reroute(aproxy, aproxy->active_playback_ausage, aproxy->active_playback_device,
                                    routed_ausage, routed_device, routed_modifier);
            } else
                set_route(aproxy, routed_ausage, routed_device, routed_modifier);

            aproxy->active_playback_ausage = routed_ausage;
            aproxy->active_playback_device = routed_device;

            // Update call-param index once routing is completed
            if (is_usage_Call(routed_ausage))
                proxy_set_call_path_param(CALL_PATH_SET, PARAM_NONE, 0);

            // Set Loopback for Playback Path
            enable_internal_path(aproxy, routed_ausage, routed_device);

            if (ausage == AUSAGE_FM_RADIO_TUNER ||
                ausage == AUSAGE_FM_RADIO_CAPTURE) {
                /* Open/Close FM Radio PCM node based on Enable/disable */
                proxy_start_fm_radio(aproxy);
            }
        } else {
            // Audio Path Routing for Capture Path
            if (aproxy->active_capture_ausage != AUSAGE_NONE &&
                aproxy->active_capture_device != DEVICE_NONE) {
                disable_internal_path(aproxy, aproxy->active_capture_ausage,
                                        aproxy->active_capture_device);
                set_reroute(aproxy, aproxy->active_capture_ausage, aproxy->active_capture_device,
                                    routed_ausage, routed_device, routed_modifier);
            } else {
                // In case of capture routing setup, it needs A-Box early-wakeup
                proxy_set_mixercontrol(aproxy, TICKLE_CONTROL, ABOX_TICKLE_ON);

                set_route(aproxy, routed_ausage, routed_device, routed_modifier);
            }

            aproxy->active_capture_ausage = routed_ausage;
            aproxy->active_capture_device = routed_device;

            // Set Loopback for Capture Path
            enable_internal_path(aproxy, routed_ausage, routed_device);

        }
    } else {
        if (routed_ausage == AUSAGE_FM_RADIO_TUNER ||
            ausage == AUSAGE_FM_RADIO_CAPTURE) {
            /* Open/Close FM Radio PCM node based on Enable/disable */
            proxy_stop_fm_radio(aproxy);
        }

        /* Do Specific Operation based on Audio Path */
        if (routed_device < DEVICE_MAIN_MIC)
            do_operations_by_playback_route_reset(aproxy);

        // Reset Loopback
        disable_internal_path(aproxy, routed_ausage, routed_device);

        // Audio Path Modifier
        if (routed_modifier != MODIFIER_NONE) {
            reset_modifier(aproxy, routed_modifier);

            if (routed_modifier < MODIFIER_BT_SCO_TX_NB)
                aproxy->active_playback_modifier = MODIFIER_NONE;
            else
                aproxy->active_capture_modifier = MODIFIER_NONE;
        } else {
            aproxy->active_playback_modifier = MODIFIER_NONE;
            aproxy->active_capture_modifier = MODIFIER_NONE;
        }

        // Audio Path Routing
        reset_route(aproxy, routed_ausage, routed_device);

        if (routed_device < DEVICE_MAIN_MIC) {
            aproxy->active_playback_ausage = AUSAGE_NONE;
            aproxy->active_playback_device = DEVICE_NONE;
        } else {
            aproxy->active_capture_ausage = AUSAGE_NONE;
            aproxy->active_capture_device = DEVICE_NONE;
        }
    }

    /* reset voicecall bandwidth change flag */
    aproxy->skip_internalpath = false;

    return true;
}

/*
 *  Proxy Voice Call Control
 */
void proxy_stop_voice_call(void *proxy)
{
    struct audio_proxy *aproxy = (struct audio_proxy *)proxy;
    voice_rx_stop(aproxy);
    voice_tx_stop(aproxy);

    return ;
}

void proxy_start_voice_call(void *proxy)
{
    struct audio_proxy *aproxy = (struct audio_proxy *)proxy;

    voice_rx_start(aproxy);

    /*
    ** Voice TX and FM Radio/Karaoke Listenback are sharing same WDMA.
    ** So, it needs to check and close WDMA when FM Radio/karaoke are working
    ** during Voice Call Start.
    */
    if (aproxy->fm_playback != NULL && aproxy->fm_capture != NULL) {
        fmradio_playback_stop(aproxy);
        fmradio_capture_stop(aproxy);
    }

    voice_tx_start(aproxy);

    return ;
}

/*
 *  Proxy FM Radio Control
 */
void proxy_stop_fm_radio(void *proxy)
{
    struct audio_proxy *aproxy = (struct audio_proxy *)proxy;

    fmradio_playback_stop(aproxy);
    fmradio_capture_stop(aproxy);

    return ;
}

void proxy_start_fm_radio(void *proxy)
{
    struct audio_proxy *aproxy = (struct audio_proxy *)proxy;

    fmradio_playback_start(aproxy);
    fmradio_capture_start(aproxy);

    return ;
}

#ifdef SEC_AUDIO_SUPPORT_LISTENBACK_DSPEFFECT
/*
 *  Proxy Karaoke Listenback Control
 */
void proxy_stop_karaoke_listenback(void *proxy)
{
    struct audio_proxy *aproxy = (struct audio_proxy *)proxy;

    /* Karaoke and FM Radio use same PCM nodes,
     ** therefore fmradio functions are used for controlling */
    fmradio_playback_stop(aproxy);
    fmradio_capture_stop(aproxy);

    return ;
}

void proxy_start_karaoke_listenback(void *proxy)
{
    struct audio_proxy *aproxy = (struct audio_proxy *)proxy;

    /* Karaoke and FM Radio use same PCM nodes,
     ** therefore fmradio functions are used for controlling */
    fmradio_playback_start(aproxy);
    fmradio_capture_start(aproxy);

    return ;
}
#endif

// General Mixer Control Functions
int proxy_get_mixer_value_int(void *proxy, const char *name)
{
    struct audio_proxy *aproxy = proxy;
    struct mixer_ctl *ctrl = NULL;
    int ret = -1;

    if (name == NULL)
        return ret;

    pthread_rwlock_rdlock(&aproxy->mixer_update_lock);

    ctrl = mixer_get_ctl_by_name(aproxy->mixer, name);
    if (ctrl) {
        ret = mixer_ctl_get_value(ctrl, 0);
    } else {
        ALOGE("proxy-%s: cannot find %s Mixer Control", __func__, name);
    }

    pthread_rwlock_unlock(&aproxy->mixer_update_lock);

    return ret;
}

int proxy_get_mixer_value_array(void *proxy, const char *name, void *value, int count)
{
    struct audio_proxy *aproxy = proxy;
    struct mixer_ctl *ctrl = NULL;
    int ret = -1;

    if (name == NULL)
        return ret;

    pthread_rwlock_rdlock(&aproxy->mixer_update_lock);

    ctrl = mixer_get_ctl_by_name(aproxy->mixer, name);
    if (ctrl) {
        ret = mixer_ctl_get_array(ctrl, value, count);
    } else {
        ALOGE("proxy-%s: cannot find %s Mixer Control", __func__, name);
    }

    pthread_rwlock_unlock(&aproxy->mixer_update_lock);

    return ret;
}

void proxy_set_mixer_value_int(void *proxy, const char *name, int value)
{
    struct audio_proxy *aproxy = proxy;
    struct mixer_ctl *ctrl = NULL;
    int ret = 0, val = value;

    if (name == NULL)
        return ;

    pthread_rwlock_rdlock(&aproxy->mixer_update_lock);

    ctrl = mixer_get_ctl_by_name(aproxy->mixer, name);
    if (ctrl) {
        ret = mixer_ctl_set_value(ctrl, 0, val);
        if (ret != 0)
            ALOGE("proxy-%s: failed to set %s", __func__, name);
    } else {
        ALOGE("proxy-%s: cannot find %s Mixer Control", __func__, name);
    }

    pthread_rwlock_unlock(&aproxy->mixer_update_lock);

    return ;
}

void proxy_set_mixer_value_string(void *proxy, const char *name, const char *value)
{
    struct audio_proxy *aproxy = proxy;
    struct mixer_ctl *ctrl = NULL;
    int ret = 0;

    if (name == NULL)
        return ;

    pthread_rwlock_rdlock(&aproxy->mixer_update_lock);

    ctrl = mixer_get_ctl_by_name(aproxy->mixer, name);
    if (ctrl) {
        ret = mixer_ctl_set_enum_by_string(ctrl, value);
        if (ret != 0) {
                ALOGE("proxy-%s: failed to set %s", __func__, name);
        }
    } else {
        ALOGE("proxy-%s: cannot find %s Mixer Control", __func__, name);
    }

    pthread_rwlock_unlock(&aproxy->mixer_update_lock);

    return ;
}

void proxy_set_mixer_value_array(void *proxy, const char *name, const void *value, int count)
{
    struct audio_proxy *aproxy = proxy;
    struct mixer_ctl *ctrl = NULL;
    int ret = 0;

    if (aproxy == NULL)
        aproxy = getInstance();

    if (name == NULL)
        return ;

    pthread_rwlock_rdlock(&aproxy->mixer_update_lock);

    ctrl = mixer_get_ctl_by_name(aproxy->mixer, name);
    if (ctrl) {
        ret = mixer_ctl_set_array(ctrl, value, count);
        if (ret != 0)
            ALOGE("proxy-%s: failed to set %s", __func__, name);
    } else {
        ALOGE("proxy-%s: cannot find %s Mixer Control", __func__, name);
    }

    pthread_rwlock_unlock(&aproxy->mixer_update_lock);

    return ;
}

void proxy_set_audio_interface(void *proxy, unsigned int interface, unsigned int sample_rate,
                               unsigned int bit_width, unsigned int channel)
{
    struct audio_proxy *aproxy = proxy;

    if (aproxy == NULL)
        return ;

    if (interface == UAIF0) {
        proxy_set_mixer_value_int(proxy, MIXER_CTL_ABOX_UAIF0_SWITCH, MIXER_OFF);
        /* SIFS0 Switch Off/On control is required only when SISF0 connected to UAIF0 */
        if (aproxy->active_playback_device == DEVICE_HEADPHONE ||
            aproxy->active_playback_device == DEVICE_HEADSET ||
            aproxy->active_playback_device == DEVICE_SPEAKER_AND_HEADPHONE ||
            aproxy->active_playback_device == DEVICE_SPEAKER_AND_HEADSET)
            proxy_set_mixer_value_int(proxy, MIXER_CTL_ABOX_SIFS0_SWITCH, MIXER_OFF);

        proxy_set_mixer_value_int(proxy, MIXER_CTL_ABOX_UAIF0_SAMPLERATE, sample_rate);
        proxy_set_mixer_value_int(proxy, MIXER_CTL_ABOX_UAIF0_WIDTH, bit_width);
        proxy_set_mixer_value_int(proxy, MIXER_CTL_ABOX_UAIF0_CHANNEL, channel);

        if (aproxy->active_playback_device == DEVICE_HEADPHONE ||
            aproxy->active_playback_device == DEVICE_HEADSET ||
            aproxy->active_playback_device == DEVICE_SPEAKER_AND_HEADPHONE ||
            aproxy->active_playback_device == DEVICE_SPEAKER_AND_HEADSET)
            proxy_set_mixer_value_int(proxy, MIXER_CTL_ABOX_SIFS0_SWITCH, MIXER_ON);

        proxy_set_mixer_value_int(proxy, MIXER_CTL_ABOX_UAIF0_SWITCH, MIXER_ON);
    } else if (interface == UAIF1) {
        proxy_set_mixer_value_int(proxy, MIXER_CTL_ABOX_UAIF1_SWITCH, MIXER_OFF);
        proxy_set_mixer_value_int(proxy, MIXER_CTL_ABOX_UAIF1_SAMPLERATE, sample_rate);
        proxy_set_mixer_value_int(proxy, MIXER_CTL_ABOX_UAIF1_WIDTH, bit_width);
        proxy_set_mixer_value_int(proxy, MIXER_CTL_ABOX_UAIF1_CHANNEL, channel);
        proxy_set_mixer_value_int(proxy, MIXER_CTL_ABOX_UAIF1_SWITCH, MIXER_ON);
    } else if (interface == UAIF2) {
        proxy_set_mixer_value_int(proxy, MIXER_CTL_ABOX_UAIF2_SWITCH, MIXER_OFF);
        proxy_set_mixer_value_int(proxy, MIXER_CTL_ABOX_UAIF2_SAMPLERATE, sample_rate);
        proxy_set_mixer_value_int(proxy, MIXER_CTL_ABOX_UAIF2_WIDTH, bit_width);
        proxy_set_mixer_value_int(proxy, MIXER_CTL_ABOX_UAIF2_CHANNEL, channel);
        proxy_set_mixer_value_int(proxy, MIXER_CTL_ABOX_UAIF2_SWITCH, MIXER_ON);
    } else if (interface == UAIF3) {
        proxy_set_mixer_value_int(proxy, MIXER_CTL_ABOX_UAIF3_SWITCH, MIXER_OFF);
        proxy_set_mixer_value_int(proxy, MIXER_CTL_ABOX_UAIF3_SAMPLERATE, sample_rate);
        proxy_set_mixer_value_int(proxy, MIXER_CTL_ABOX_UAIF3_WIDTH, bit_width);
        proxy_set_mixer_value_int(proxy, MIXER_CTL_ABOX_UAIF3_CHANNEL, channel);
        proxy_set_mixer_value_int(proxy, MIXER_CTL_ABOX_UAIF3_SWITCH, MIXER_ON);
    }

    return ;
}

// Specific Mixer Control Functions
void proxy_set_audiomode(void *proxy, int audiomode)
{
    struct audio_proxy *aproxy = proxy;
    struct mixer_ctl *ctrl = NULL;
    int ret = 0, val = audiomode;

    if ((aproxy->audio_mode == AUDIO_MODE_IN_CALL ||
        aproxy->audio_mode == AUDIO_MODE_IN_COMMUNICATION) &&
        audiomode == AUDIO_MODE_NORMAL)
        clr_call_path_param();

    aproxy->audio_mode = val; // set audio mode
    pthread_rwlock_rdlock(&aproxy->mixer_update_lock);

    /* Set Audio Mode to Kernel */
    ctrl = mixer_get_ctl_by_name(aproxy->mixer, ABOX_AUDIOMODE_CONTROL_NAME);
    if (ctrl) {
        ret = mixer_ctl_set_value(ctrl, 0,val);
        if (ret != 0)
            ALOGE("proxy-%s: failed to set Android AudioMode to Kernel", __func__);
    } else {
        ALOGE("proxy-%s: cannot find AudioMode Mixer Control", __func__);
    }

    pthread_rwlock_unlock(&aproxy->mixer_update_lock);

    return ;
}

void proxy_set_volume(void *proxy, int volume_type, float left, float right)
{
    struct audio_proxy *aproxy = proxy;
    struct mixer_ctl *ctrl = NULL;
    int ret = -ENAVAIL;
    int val[2];

    pthread_rwlock_rdlock(&aproxy->mixer_update_lock);

    if (volume_type == VOLUME_TYPE_MMAP) {
        val[0] = (int)(left * MMAP_PLAYBACK_VOLUME_MAX);
        val[1] = (int)(right * MMAP_PLAYBACK_VOLUME_MAX);

        ctrl = mixer_get_ctl_by_name(aproxy->mixer, MIXER_CTL_ABOX_MMAP_OUT_VOLUME_CONTROL);
    } else if (volume_type == VOLUME_TYPE_CALL) {
        val[0] = (int)(left * CALL_PLAYBACK_VOLUME_MAX);

        ctrl = mixer_get_ctl_by_name(aproxy->mixer, CALL_VOLUME_CONTROL_NAME);
    } else if (volume_type == VOLUME_TYPE_CALL_TX_MUTE) {
        val[0] = (left != 0.0) ? 1 : 0;

        ctrl = mixer_get_ctl_by_name(aproxy->mixer, CALL_TX_MUTE_CONTROL_NAME);
    } else if (volume_type == VOLUME_TYPE_CALL_RX_MUTE) {
        val[0] = (left != 0.0) ? 1 : 0;

        ctrl = mixer_get_ctl_by_name(aproxy->mixer, CALL_RX_MUTE_CONTROL_NAME);
	}

    if (ctrl) {
        switch(volume_type) {
            case VOLUME_TYPE_OFFLOAD:
                ret = mixer_ctl_set_array(ctrl, val, sizeof(val)/sizeof(val[0]));
                break;
            case VOLUME_TYPE_MMAP:
            case VOLUME_TYPE_CALL:
            case VOLUME_TYPE_CALL_TX_MUTE:
            case VOLUME_TYPE_CALL_RX_MUTE:
                ret = mixer_ctl_set_value(ctrl, 0, val[0]);
                break;
        }

        if (ret != 0)
            ALOGE("proxy-%s: failed to set Volume", __func__);
        else
            ALOGV("proxy-%s: set Volume(%f:%f) => (%d:%d)", __func__, left, right, val[0], val[1]);
    } else {
        ALOGE("proxy-%s: cannot find Volume Control", __func__);
    }

    pthread_rwlock_unlock(&aproxy->mixer_update_lock);

    return;
}

void proxy_clear_apcall_txse(void)
{
    struct audio_proxy *aproxy = getInstance();
    char basic_path_name[MAX_PATH_NAME_LEN];
    char path_name[MAX_PATH_NAME_LEN];
    audio_usage ausage = aproxy->active_capture_ausage;

    memset(path_name, 0, MAX_PATH_NAME_LEN);

    if (snprintf(path_name, MAX_PATH_NAME_LEN - 1, "set-%s-txse", usage_path_table[ausage]) < 0) {
        ALOGE("proxy-%s: path name has error: %s", __func__, strerror(errno));
        return;
    }

    pthread_rwlock_rdlock(&aproxy->mixer_update_lock);

    audio_route_reset_and_update_path(aproxy->aroute, path_name);
    ALOGI("proxy-%s: %s is disabled", __func__, path_name);

    pthread_rwlock_unlock(&aproxy->mixer_update_lock);

    return ;
}

void proxy_set_apcall_txse(void)
{
    struct audio_proxy *aproxy = getInstance();
    char basic_path_name[MAX_PATH_NAME_LEN];
    char path_name[MAX_PATH_NAME_LEN];
    audio_usage ausage = aproxy->active_capture_ausage;

    memset(path_name, 0, MAX_PATH_NAME_LEN);

    if (snprintf(path_name, MAX_PATH_NAME_LEN - 1, "set-%s-txse", usage_path_table[ausage]) < 0) {
        ALOGE("proxy-%s: path name has error: %s", __func__, strerror(errno));
        return;
    }

    pthread_rwlock_rdlock(&aproxy->mixer_update_lock);

    audio_route_apply_and_update_path(aproxy->aroute, path_name);
    ALOGI("proxy-%s: %s is enabled", __func__, path_name);

    pthread_rwlock_unlock(&aproxy->mixer_update_lock);

    return ;
}

void proxy_set_upscale(void *proxy, int sampling_rate, int pcm_format)
{
    struct audio_proxy *aproxy = proxy;
    struct mixer_ctl *ctrl = NULL;
    int ret = 0, val = (int)UPSCALE_NONE;

    pthread_rwlock_rdlock(&aproxy->mixer_update_lock);

    /* Set Compress Offload Upscaling Info to Kernel */
    ctrl = mixer_get_ctl_by_name(aproxy->mixer, OFFLOAD_UPSCALE_CONTROL_NAME);
    if (ctrl) {
        if (sampling_rate == 48000 && (audio_format_t)pcm_format == AUDIO_FORMAT_PCM_SUB_16_BIT)
            val = (int)UPSCALE_48K_16B;
        else if ((audio_format_t)pcm_format == AUDIO_FORMAT_PCM_SUB_16_BIT) {
            if (sampling_rate == 48000)
                val = (int)UPSCALE_48K_24B;
            else if (sampling_rate == 192000)
                val = (int)UPSCALE_192K_24B;
            else if (sampling_rate == 384000)
                val = (int)UPSCALE_384K_24B;
        }

        if (val != (int)UPSCALE_NONE) {
            ret = mixer_ctl_set_value(ctrl, 0, val);
            if (ret != 0)
                ALOGE("proxy-%s: failed to set Offload Upscale Info to Kernel", __func__);
            else
                ALOGV("proxy-%s: set Offload Upscale Info as %d", __func__, val);
        } else
            ALOGE("proxy-%s: invalid Offload Upscale Info", __func__);
    } else {
        ALOGE("proxy-%s: cannot find Offload Upscale Info Mixer Control", __func__);
    }

    pthread_rwlock_unlock(&aproxy->mixer_update_lock);

    return;
}

void proxy_call_status(void *proxy, int status)
{
    struct audio_proxy *aproxy = (struct audio_proxy *)proxy;

    /* status : TRUE means call starting
        FALSE means call stopped
    */
    if (status)
        aproxy->call_state = true;
    else
        aproxy->call_state = false;

    return;
}

int proxy_set_parameters(void *proxy, void *parameters)
{
    struct audio_proxy *aproxy = (struct audio_proxy *)proxy;
    struct str_parms *parms = (struct str_parms *)parameters;
    char value[256];
    int val;
    int ret = 0;     // for parameter handling
    int status = 0;  // for return value

    ret = str_parms_get_int(parms, AUDIO_PARAMETER_DEVICE_CONNECT, &val);
    if (ret >= 0) {
        if ((audio_devices_t)val == AUDIO_DEVICE_IN_WIRED_HEADSET) {
            ALOGD("proxy-%s: Headset Device connected 0x%x", __func__, val);
        } else if ((audio_devices_t)val == AUDIO_DEVICE_OUT_BLUETOOTH_A2DP ||
                   (audio_devices_t)val == AUDIO_DEVICE_OUT_BLUETOOTH_A2DP_HEADPHONES ||
                   (audio_devices_t)val == AUDIO_DEVICE_OUT_BLUETOOTH_A2DP_SPEAKER) {
            ALOGI("proxy-%s: connected BT A2DP Out Device", __func__);
        } else if ((audio_devices_t)val == AUDIO_DEVICE_OUT_AUX_DIGITAL) {
            aproxy->is_dp_connected = true;
            ALOGI("proxy-%s: AUX_DIGITAL Out Device connected", __func__);
        }
    }

    ret = str_parms_get_int(parms, AUDIO_PARAMETER_DEVICE_DISCONNECT, &val);
    if (ret >= 0) {
        if ((audio_devices_t)val == AUDIO_DEVICE_IN_WIRED_HEADSET) {
            ALOGD("proxy-%s: Headset Device disconnected 0x%x", __func__, val);
        } else if ((audio_devices_t)val == AUDIO_DEVICE_OUT_BLUETOOTH_A2DP ||
                   (audio_devices_t)val == AUDIO_DEVICE_OUT_BLUETOOTH_A2DP_HEADPHONES ||
                   (audio_devices_t)val == AUDIO_DEVICE_OUT_BLUETOOTH_A2DP_SPEAKER) {
            ALOGI("proxy-%s: disconnected BT A2DP Out Device", __func__);
        } else if ((audio_devices_t)val == AUDIO_DEVICE_OUT_AUX_DIGITAL) {
            aproxy->is_dp_connected = false;
            ALOGI("proxy-%s: AUX_DIGITAL Out Device disconnected", __func__);
        }
    }

    return status;
}

int proxy_get_microphones(void *proxy, void *array, int *count)
{
    struct audio_proxy *aproxy = (struct audio_proxy *)proxy;
    struct audio_microphone_characteristic_t *mic_array = array;
    size_t *mic_count = (size_t *)count;
    size_t actual_mic_count = 0;
    int ret = 0;

    if (aproxy) {
        if (*mic_count == 0) {
            *mic_count = (size_t)aproxy->num_mic;
            ALOGI("proxy-%s: requested number of microphone, return %zu", __func__, *mic_count);
        } else {
            for (int i = 0; i < aproxy->num_mic; i++) {
                mic_array[i] = aproxy->mic_info[i];
                ALOGD("proxy-%s: %dth MIC = %s", __func__, i+1, mic_array[i].device_id);
                actual_mic_count++;
            }
            *mic_count = actual_mic_count;
        }
    } else {
        ALOGE("proxy-%s: aproxy is NULL", __func__);
        ret = -ENOSYS;
    }

    return ret;
}

void proxy_set_uhqa_stream_config(void *proxy_stream, bool config)
{
    struct audio_proxy_stream *apstream = (struct audio_proxy_stream *)proxy_stream;

    if (apstream)
        apstream->need_update_pcm_config = config;
}

bool proxy_get_uhqa_stream_config(void *proxy_stream)
{
    struct audio_proxy_stream *apstream = (struct audio_proxy_stream *)proxy_stream;
    bool uhqa_stream_config = false;

    if (apstream)
        uhqa_stream_config = apstream->need_update_pcm_config;

    return uhqa_stream_config;
}

void proxy_set_dual_speaker_mode(void *proxy, bool state)
{
    struct audio_proxy *aproxy = proxy;
    aproxy->support_dualspk = state;
}

void proxy_set_stream_channel(void *proxy_stream, int new_channel, bool skip)
{
    struct audio_proxy_stream *apstream = (struct audio_proxy_stream *)proxy_stream;

    if (new_channel > 0) {
        apstream->pcmconfig.channels = new_channel;
    }
    apstream->skip_ch_convert = skip;
    apstream->need_channelconversion = !skip;
    ALOGI("%s: new_channel %d, skip_ch_convert %d", __func__, new_channel, apstream->skip_ch_convert);
}

void proxy_set_spk_ampL_power(void* proxy, bool state)
{
    struct audio_proxy *aproxy = proxy;

    ALOGI("proxy-%s: enable upper-spk-amp-%s", __func__, state ? "unmute" : "mute");
    aproxy->spk_ampL_powerOn = state;

    // T:Far   - ampL on, L mute off
    // F:Close - ampL off, L mute on
    if (aproxy->support_dualspk) {
        if (!aproxy->spk_ampL_powerOn)
            audio_route_apply_and_update_path(aproxy->aroute, "upper-spk-amp-mute");
        else
            audio_route_apply_and_update_path(aproxy->aroute, "upper-spk-amp-unmute");
    }
}

bool proxy_get_spk_ampL_power(void* proxy)
{
    struct audio_proxy *aproxy = proxy;
    return aproxy->spk_ampL_powerOn;
}

void proxy_set_primary_mute(void* proxy, int count)
{
    struct audio_proxy *aproxy = proxy;
    struct mixer_ctl *ctrl = NULL;
    char mixer_name[MAX_MIXER_NAME_LEN];
    int ret = 0, val = count;

    pthread_rwlock_rdlock(&aproxy->mixer_update_lock);

    ctrl = mixer_get_ctl_by_name(aproxy->mixer, ABOX_MUTE_CONTROL_NAME);
    snprintf(mixer_name, sizeof(mixer_name), ABOX_MUTE_CONTROL_NAME);

    if (ctrl) {
        ret = mixer_ctl_set_value(ctrl, 0,val);
        if (ret != 0)
            ALOGE("proxy-%s: failed to set primary mute(%s)", __func__, mixer_name);
        else
            ALOGI("proxy-%s: set set primary mute(%s) to %d", __func__, mixer_name, val);
    } else {
        ALOGE("proxy-%s: cannot find primary mute", __func__);
    }

    pthread_rwlock_unlock(&aproxy->mixer_update_lock);

    return ;
}

/*
 *  Proxy Dump
 */
int proxy_fw_dump(int fd)
{
    ALOGV("proxy-%s: enter with file descriptor(%d)", __func__, fd);

#ifdef SEC_AUDIO_SOUND_TRIGGER_ENABLED
    struct audio_proxy *aproxy = getInstance();
    const size_t len = 256;
    char buffer[len];

    snprintf(buffer, len, "\tsthal_state : 0x%x\n", aproxy->sthal_state);
    write(fd, buffer, strlen(buffer));
#endif

    calliope_ramdump(fd);

    ALOGV("proxy-%s: exit with file descriptor(%d)", __func__, fd);

    return 0;
}

static bool find_enum_from_string(struct audio_string_to_enum *table, const char *name,
                                  int32_t table_cnt, int *value)
{
    int i;

    for (i = 0; i < table_cnt; i++) {
        if (strcmp(table[i].name, name) == 0) {
            *value = table[i].value;
            return true;
        }
    }
    return false;
}

static void set_microphone_info(struct audio_microphone_characteristic_t *microphone, const XML_Char **attr)
{
    uint32_t curIdx = 0;
    uint32_t array_cnt = 0;
    float f_value[3] = {0, };
    char *ptr = NULL;

    if (strcmp(attr[curIdx++], "device_id") == 0)
        strcpy(microphone->device_id, attr[curIdx++]);

    if (strcmp(attr[curIdx++], "id") == 0)
        microphone->id = atoi(attr[curIdx++]);

    if (strcmp(attr[curIdx++], "device") == 0)
        find_enum_from_string(device_in_type, attr[curIdx++], ARRAY_SIZE(device_in_type), (int *)&microphone->device);

    if (strcmp(attr[curIdx++], "address") == 0)
        strcpy(microphone->address, attr[curIdx++]);

    if (strcmp(attr[curIdx++], "location") == 0)
        find_enum_from_string(microphone_location, attr[curIdx++], AUDIO_MICROPHONE_LOCATION_CNT, (int *)&microphone->location);

    if (strcmp(attr[curIdx++], "group") == 0)
        microphone->group = atoi(attr[curIdx++]);

    if (strcmp(attr[curIdx++], "index_in_the_group") == 0)
        microphone->index_in_the_group = atoi(attr[curIdx++]);

    if (strcmp(attr[curIdx++], "sensitivity") == 0)
        microphone->sensitivity = atof(attr[curIdx++]);

    if (strcmp(attr[curIdx++], "max_spl") == 0)
        microphone->max_spl = atof(attr[curIdx++]);

    if (strcmp(attr[curIdx++], "min_spl") == 0)
        microphone->min_spl = atof(attr[curIdx++]);

    if (strcmp(attr[curIdx++], "directionality") == 0)
        find_enum_from_string(microphone_directionality, attr[curIdx++],
                              AUDIO_MICROPHONE_LOCATION_CNT, (int *)&microphone->directionality);

    if (strcmp(attr[curIdx++], "num_frequency_responses") == 0) {
        microphone->num_frequency_responses = atoi(attr[curIdx++]);
        if (microphone->num_frequency_responses > 0) {
            if (strcmp(attr[curIdx++], "frequencies") == 0) {
                ptr = strtok((char *)attr[curIdx++], " ");
                while(ptr != NULL) {
                    microphone->frequency_responses[0][array_cnt++] = atof(ptr);
                    ptr = strtok(NULL, " ");
                }
            }
            array_cnt = 0;
            if (strcmp(attr[curIdx++], "responses") == 0) {
                ptr = strtok((char *)attr[curIdx++], " ");
                while(ptr != NULL) {
                    microphone->frequency_responses[1][array_cnt++] = atof(ptr);
                    ptr = strtok(NULL, " ");
                }
            }
        }
    }

    if (strcmp(attr[curIdx++], "geometric_location") == 0) {
        ptr = strtok((char *)attr[curIdx++], " ");
        array_cnt = 0;
        while (ptr != NULL) {
            f_value[array_cnt++] = atof(ptr);
            ptr = strtok(NULL, " ");
        }
        microphone->geometric_location.x = f_value[0];
        microphone->geometric_location.y = f_value[1];
        microphone->geometric_location.z = f_value[2];
    }

    if (strcmp(attr[curIdx++], "orientation") == 0) {
        ptr = strtok((char *)attr[curIdx++], " ");
        array_cnt = 0;
        while (ptr != NULL) {
            f_value[array_cnt++] = atof(ptr);
            ptr = strtok(NULL, " ");
        }
        microphone->orientation.x = f_value[0];
        microphone->orientation.y = f_value[1];
        microphone->orientation.z = f_value[2];
    }

    /* Channel mapping doesn't used for now. */
    for (array_cnt = 0; array_cnt < AUDIO_CHANNEL_COUNT_MAX; array_cnt++)
        microphone->channel_mapping[array_cnt] = AUDIO_MICROPHONE_CHANNEL_MAPPING_UNUSED;
}

static void end_tag(void *data, const XML_Char *tag_name)
{
    if (strcmp(tag_name, "microphone_characteristis") == 0)
        set_info = INFO_NONE;
}

static void start_tag(void *data, const XML_Char *tag_name, const XML_Char **attr)
{
    struct audio_proxy *aproxy = getInstance();
    const XML_Char *attr_name  = NULL;
    const XML_Char *attr_value = NULL;

    if (strcmp(tag_name, "microphone_characteristics") == 0) {
        set_info = MICROPHONE_CHARACTERISTIC;
    } else if (strcmp(tag_name, "microphone") == 0) {
        if (set_info != MICROPHONE_CHARACTERISTIC) {
            ALOGE("proxy-%s: microphone tag should be supported with microphone_characteristics tag", __func__);
            return ;
        }
        set_microphone_info(&aproxy->mic_info[aproxy->num_mic++], attr);
    }
}

void proxy_set_board_info(void *proxy)
{
    struct audio_proxy *aproxy = (struct audio_proxy *)proxy;
    XML_Parser parser = 0;
    FILE *file = NULL;
    char info_file_name[MAX_MIXER_NAME_LEN] = {0};
    void *buf = NULL;
    uint32_t buf_size = 1024;
    int32_t bytes_read = 0;

    strlcpy(info_file_name, BOARD_INFO_XML_PATH, MAX_MIXER_NAME_LEN);

    file = fopen(info_file_name, "r");
    if (file == NULL)
        ALOGE("proxy-%s: open error: %s, file=%s", __func__, strerror(errno), info_file_name);
    else
        ALOGI("proxy-%s: Board info file name is %s", __func__, info_file_name);

    parser = XML_ParserCreate(NULL);

    XML_SetElementHandler(parser, start_tag, end_tag);

    while (1) {
        buf = XML_GetBuffer(parser, buf_size);
        if (buf == NULL) {
            ALOGE("proxy-%s fail to get buffer", __func__);
            break;
        }

        bytes_read = fread(buf, 1, buf_size, file);
        if (bytes_read < 0) {
            ALOGE("proxy-%s fail to read from file", __func__);
            break;
        }

        XML_ParseBuffer(parser, bytes_read, bytes_read == 0);

        if (bytes_read == 0)
            break;
    }

    XML_ParserFree(parser);
    fclose(file);
}

bool proxy_is_initialized(void)
{
    if (instance)
        return true;
    else
        return false;
}

void * proxy_init(void)
{
    struct audio_proxy *aproxy;

    /* Creates the structure for audio_proxy. */
    aproxy = getInstance();
    if (!aproxy) {
        ALOGE("proxy-%s: failed to create for audio_proxy", __func__);
        return NULL;
    }

    aproxy->primary_out = NULL;

    // In case of External BT-SCO Support, initializes Playback Stream
    aproxy->support_btsco = true;
    aproxy->btsco_playback = NULL;

    // Voice Call PCM Devices
    aproxy->call_rx = NULL;
    aproxy->call_tx = NULL;
    aproxy->call_tx_direct = NULL;

    // FM Radio PCM Devices
    aproxy->fm_playback = NULL;
    aproxy->fm_capture  = NULL;

    // Call State
    aproxy->call_state = false;
    aproxy->skip_internalpath = false;

    /* Audio Mode */
    aproxy->audio_mode = AUDIO_MODE_NORMAL;

    /* offload effect */
    aproxy->offload_effect_lib = NULL;
    aproxy->offload_effect_lib_update = NULL;

    /* dualspk */
    aproxy->spk_ampL_powerOn = false;

#ifdef SEC_AUDIO_DUMP
    aproxy->input_cnt = 0;
#endif

    /* Display port(DP) connection status */
    aproxy->is_dp_connected = false;

    proxy_set_board_info(aproxy);

    ALOGI("proxy-%s: opened & initialized Audio Proxy", __func__);
    return (void *)aproxy;
}

void proxy_deinit(void *proxy __unused)
{
    struct audio_proxy *aproxy = (struct audio_proxy *)proxy;

    if (aproxy) {
        destroyInstance();
        ALOGI("proxy-%s: destroyed for audio_proxy", __func__);
    }
    return ;
}

/******************************************************************************/
/**                                                                          **/
/** The ODM Proxy Functions                                                  **/
/**                                                                          **/
/******************************************************************************/
#ifdef SEC_AUDIO_SAMSUNGRECORD
void proxy_set_stream_format(void *proxy_stream, audio_format_t new_format, bool skip)
{
    struct audio_proxy_stream *apstream = (struct audio_proxy_stream *)proxy_stream;

    if (new_format != AUDIO_FORMAT_INVALID) {
        apstream->pcmconfig.format = pcm_format_from_audio_format(new_format);
    }
    apstream->skip_format_convert = skip;
    check_conversion(apstream);
    ALOGI("%s: new_format %d, skip_format_convert %d", __func__, new_format, apstream->skip_format_convert);
}

uint32_t proxy_get_last_channel_count(void *proxy_stream)
{
    struct audio_proxy_stream *apstream = (struct audio_proxy_stream *)proxy_stream;

    int channel_cnt = apstream->skip_ch_convert ?
        proxy_get_actual_channel_count(apstream)
        : audio_channel_count_from_in_mask(apstream->requested_channel_mask);

    return channel_cnt;
}

int32_t proxy_get_last_format(void *proxy_stream)
{
    struct audio_proxy_stream *apstream = (struct audio_proxy_stream *)proxy_stream;

    audio_format_t format = apstream->skip_format_convert ?
        proxy_get_actual_format(apstream) : apstream->requested_format;

    return format;
}
#endif

#ifdef SEC_AUDIO_DUMP
/******************************************************************************/
/**                                                                          **/
/** The pcm_dump Functions                                               **/
/**                                                                          **/
/******************************************************************************/

void out_get_pcm_dump(void *proxy_stream, const void* buffer, size_t bytes, bool before_solution)
{
    struct audio_proxy_stream *apstream = (struct audio_proxy_stream *)proxy_stream;

    int32_t samplerate = (int32_t)apstream->pcmconfig.rate;
    int32_t format = AUDIO_FORMAT_INVALID;
    int32_t channels = (int32_t)apstream->pcmconfig.channels;

    // set format
    if (apstream->requested_format == AUDIO_FORMAT_DSD) {
        format = (int32_t) AUDIO_FORMAT_DSD;
    } else if (apstream->pcmconfig.format == PCM_FORMAT_S32_LE) {
        format = (int32_t) AUDIO_FORMAT_PCM_32_BIT;
    } else if (apstream->pcmconfig.format == PCM_FORMAT_S24_LE) {
        format = (int32_t) AUDIO_FORMAT_PCM_8_24_BIT;
    } else {
        format = (int32_t) AUDIO_FORMAT_PCM_16_BIT;
    }

    sec_pcm_write(
        buffer,
        bytes,
        PCM_DUMP_USE_PLAYBACK,
        PCM_DUMP_POINT_HAL,
        PCM_DUMP_FLAG_AUTO_SAVE,
        format,
        samplerate,
        channels,
        "out%s%s", (apstream->stream_type == ASTREAM_PLAYBACK_PRIMARY ? "_primary" :
                    apstream->stream_type == ASTREAM_PLAYBACK_DEEP_BUFFER ? "_deep" :
                    apstream->stream_type == ASTREAM_PLAYBACK_AUX_DIGITAL ? "_multi_ch" :
                    apstream->requested_format == AUDIO_FORMAT_DSD ? "" : "_low"),
                    (before_solution ? "_pure" : "_last"));
}

void out_stop_pcm_dump(void *proxy_stream __unused)
{
    // stop all playback hal dump
    sec_pcm_stop(
        PCM_DUMP_USE_PLAYBACK,
        PCM_DUMP_POINT_HAL,
        PCM_DUMP_FLAG_MULTI_STOP,
        PCM_DUMP_CONFIG_NOT_SET,
        PCM_DUMP_CONFIG_NOT_SET,
        PCM_DUMP_CONFIG_NOT_SET,
        PCM_DUMP_CONFIG_NOT_SET);
}

static void get_pcm_dump_name(void *proxy_stream, pcm_dump_type type, char *filePath)
{
    struct audio_proxy_stream *apstream = (struct audio_proxy_stream *)proxy_stream;

    switch(type) {
        case PCM_DUMP_PURE:
            sprintf(filePath, "hal_in_pure_%dbit_%dk_%dch_%d",
                (apstream->pcmconfig.format == PCM_FORMAT_S24_LE) ? 24 : 16,
                apstream->pcmconfig.rate, apstream->pcmconfig.channels, apstream->input_count);
        break;
        case PCM_DUMP_LAST:
            sprintf(filePath, "hal_in_last_%dbit_%dk_%dch_%d",
                (apstream->requested_format == AUDIO_FORMAT_PCM_SUB_8_24_BIT) ? 24 : 16,
                apstream->requested_sample_rate, popcount(apstream->requested_channel_mask), apstream->input_count);
        break;
        case PCM_DUMP_SEAMLESS:
            sprintf(filePath, "hal_in_seamless_%dk_%dch_%d",
                apstream->requested_sample_rate, popcount(apstream->requested_channel_mask), apstream->input_count);
        break;
        case PCM_DUMP_VOICENOTE:
            sprintf(filePath, "hal_in_voice_note_16k_1ch_%d", apstream->input_count);
        break;
        case PCM_DUMP_CONVERT:
            sprintf(filePath, "hal_in_convert_%dk_%dch_%d",
                apstream->requested_sample_rate,
                (apstream->skip_ch_convert) ? apstream->pcmconfig.channels : popcount(apstream->requested_channel_mask),
                apstream->input_count);
        break;
        case PCM_DUMP_CALLREC:
            sprintf(filePath, "hal_in_callrec_%dk_%dch_%d",
                apstream->requested_sample_rate, popcount(apstream->requested_channel_mask), apstream->input_count);
        break;
        default:
            return;
    }
}

void in_get_pcm_dump(void *proxy_stream, const void* buffer, size_t bytes, pcm_dump_type type)
{
    char filePath[256]={0,};

    get_pcm_dump_name(proxy_stream, type, filePath);
    sec_rec_pcm_dump(filePath, buffer, bytes, PCM_DUMP_HAL);
}

void in_stop_pcm_dump(void *proxy_stream)
{
    struct audio_proxy_stream *apstream = (struct audio_proxy_stream *)proxy_stream;
    char filePath[256]={0,};

    for (int i = 0; i <= PCM_DUMP_CALLREC; ++i) {
        get_pcm_dump_name(proxy_stream, i, filePath);
        sec_pcm_dump_stop(filePath, PCM_DUMP_HAL);
    }
}

#endif
