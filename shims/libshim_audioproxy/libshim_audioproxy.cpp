/*
 * Copyright (C) 2020-2025 The LineageOS Project
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

#include <errno.h>
#include <sound/asound.h>
#include <unistd.h>

extern "C" int get_dai_link(int /*param_1*/, int /*param_2*/) {
    /*if (param_1 != 0) {
      __android_log_print(2,"audio_route","requested PCM for %d",param_2);
    }*/
    return -1;
}

// possibly audio_route_missing_ctl(struct audio_route *ar)
extern "C" int audio_route_missing_ctl(int* param_1) {
    if (param_1 == 0) {
        //__android_log_print(6,"audio_route","invalid audio_route");
        return 0;
    }
    int a = *(int*)(param_1 + 6);
    return a;
}

struct mixer {
    int fd;
    struct snd_ctl_card_info card_info;
    struct snd_ctl_elem_info* elem_info;
    struct mixer_ctl* ctl;
    unsigned int count;
};

struct audio_route {
    struct mixer* mixer;
    unsigned int num_mixer_ctls;
    struct mixer_state* mixer_state;

    unsigned int mixer_path_size;
    unsigned int num_mixer_paths;
    struct mixer_path* mixer_path;
};

extern "C" struct snd_ctl_event *mixer_read_event_sec(struct mixer *mixer, unsigned int mask)
{
    struct snd_ctl_event* ev = NULL;

    if (!mixer)
        return 0;

    ev = (struct snd_ctl_event*)calloc(1, sizeof(*ev));
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
