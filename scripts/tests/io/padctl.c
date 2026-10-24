// padctl — uinput virtual gamepad E2E verification tool.
// Usage: padctl test    -> runs the full create/inject/verify/destroy cycle
//        padctl create <name> <vid> <pid>  -> prints fd
//        padctl inject <fd> <type> <code> <val>
//        padctl destroy <fd>
#include <linux/uinput.h>
#include <linux/input.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <errno.h>
#include <pthread.h>

static int create_pad(const char *name, uint16_t vid, uint16_t pid) {
    int fd = open("/dev/uinput", O_RDWR | O_NONBLOCK);
    if (fd < 0) { perror("open /dev/uinput"); return -1; }
    ioctl(fd, UI_SET_EVBIT, EV_SYN);
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_EVBIT, EV_ABS);
    for (unsigned k = 304; k <= 318; k++) ioctl(fd, UI_SET_KEYBIT, k);
    for (unsigned k = 544; k <= 547; k++) ioctl(fd, UI_SET_KEYBIT, k);
    ioctl(fd, UI_SET_ABSBIT, ABS_X);
    ioctl(fd, UI_SET_ABSBIT, ABS_Y);
    ioctl(fd, UI_SET_ABSBIT, ABS_RX);
    ioctl(fd, UI_SET_ABSBIT, ABS_RY);
    ioctl(fd, UI_SET_ABSBIT, ABS_HAT0X);
    ioctl(fd, UI_SET_ABSBIT, ABS_HAT0Y);
    ioctl(fd, UI_SET_FFBIT, FF_RUMBLE);
    struct uinput_setup us;
    memset(&us, 0, sizeof(us));
    strncpy(us.name, name, 79);
    us.id.bustype = BUS_USB;
    us.id.vendor = vid;
    us.id.product = pid;
    us.ff_effects_max = 16;
    if (ioctl(fd, UI_DEV_SETUP, &us) < 0) { perror("UI_DEV_SETUP"); close(fd); return -1; }
    if (ioctl(fd, UI_DEV_CREATE) < 0) { perror("UI_DEV_CREATE"); close(fd); return -1; }
    int high = fcntl(fd, F_DUPFD, 500);
    if (high >= 500) { close(fd); fd = high; }
    return fd;
}

static int find_evnode(const char *devname) {
    // Find the event node whose device name matches.
    // Retry for up to 2s: the devtmpfs node appears asynchronously after
    // UI_DEV_CREATE.
    char path[128];
    for (int attempt = 0; attempt < 20; attempt++) {
      for (int i = 39; i >= 0; i--) {
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        char nm[256];
        if (ioctl(fd, EVIOCGNAME(255), nm) >= 0) {
            if (strcmp(nm, devname) == 0) { return fd; }
        }
        close(fd);
      }
      usleep(100000);
    }
    return -1;
}


struct ff_args { int ufd; int evfd; volatile int done; };
static void *uploader(void *p) {
    struct ff_args *a = p;
    struct ff_effect eff; memset(&eff, 0, sizeof(eff));
    // Byte layout mirrors gamepad.hv rumble(): type@0, id@2=-1, direction@4,
    // u.rumble strong@16, weak@18 (offsetof(u.rumble)==16).
    eff.type = FF_RUMBLE; eff.id = -1; eff.direction = 0x4000;
    eff.u.rumble.strong_magnitude = 32768; eff.u.rumble.weak_magnitude = 16384;
    int r = ioctl(a->evfd, EVIOCSFF, &eff);
    printf("FF_UPLOAD ret=%d id=%d errno=%d\n", r, eff.id, r < 0 ? errno : 0);
    a->done = 1;
    if (r >= 0) {
        struct input_event ev; memset(&ev, 0, sizeof(ev));
        ev.type = EV_FF; ev.code = eff.id; ev.value = 1;
        write(a->ufd, &ev, sizeof(ev));
        memset(&ev, 0, sizeof(ev)); ev.type = EV_FF; ev.code = eff.id; ev.value = 0;
        write(a->ufd, &ev, sizeof(ev));
        printf("FF_PLAY=1\n");
    }
    return NULL;
}
static void ff_mode(int ufd, int evfd) {
    pthread_t t;
    struct ff_args a = { ufd, evfd, 0 };
    pthread_create(&t, NULL, uploader, &a);
    // service the ack handshake on the uinput fd. The fd is O_NONBLOCK, so
    // poll with a deadline until the uploader finishes.
    struct input_event ev;
    for (int tries = 0; tries < 500 && !a.done; tries++) {
        ssize_t n = read(ufd, &ev, sizeof(ev));
        if (tries < 5) printf("read#%d n=%zd errno=%d\n", tries, n, n < 0 ? errno : 0);
        if (n != (ssize_t)sizeof(ev)) { usleep(10000); continue; }
        printf("UIN-EV: type=%u code=%u value=%d\n", ev.type, ev.code, ev.value);
        if (ev.type == EV_UINPUT) {
            if (ev.code == UI_FF_UPLOAD) {
                struct uinput_ff_upload up; memset(&up, 0, sizeof(up));
                up.request_id = ev.value;
                ioctl(ufd, UI_BEGIN_FF_UPLOAD, &up);
                printf("FF_ACK begin id=%u type=%d\n", up.request_id, up.effect.type);
                up.retval = 0;
                ioctl(ufd, UI_END_FF_UPLOAD, &up);
            }
        } else if (ev.type == EV_FF) {
            printf("FF_EVENT code=%d value=%d\n", ev.code, ev.value);
        }
    }
    pthread_join(t, NULL);
}

int main(int argc, char **argv) {
    if (argc < 2) return 1;
    if (strcmp(argv[1], "test") == 0) {
        // Full E2E: create, find event node, inject, verify, rumble, destroy
        int ufd = create_pad("Wireless Controller", 1356, 3302);
        if (ufd < 0) { printf("FAIL: create\n"); return 1; }
        printf("PAD_CREATED fd=%d (F_GETFD=%d)\n", ufd, fcntl(ufd, F_GETFD));

        int evfd = find_evnode("Wireless Controller");
        if (evfd < 0) { printf("FAIL: event node not found\n"); close(ufd); return 1; }
        printf("EVENT_NODE fd=%d (ufd F_GETFD=%d)\n", evfd, fcntl(ufd, F_GETFD));

        // Inject BTN_SOUTH press
        struct input_event ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = EV_KEY; ev.code = BTN_SOUTH; ev.value = 1;
        if (write(ufd, &ev, sizeof(ev)) < 0) { printf("FAIL: press write\n"); }
        memset(&ev, 0, sizeof(ev));
        ev.type = EV_SYN; ev.code = SYN_REPORT; ev.value = 0;
        write(ufd, &ev, sizeof(ev));
        usleep(50000);

        // Read the press event from the evdev node
        struct input_event read_ev;
        int got_press = 0;
        for (int i = 0; i < 20; i++) {
            ssize_t n = read(evfd, &read_ev, sizeof(read_ev));
            if (n != (ssize_t)sizeof(read_ev)) break;
            if (read_ev.type == EV_KEY && read_ev.code == BTN_SOUTH && read_ev.value == 1) {
                got_press = 1; break;
            }
        }
        printf("PRESS_READ=%d\n", got_press);
        fflush(stdout);
        // Inject release
        memset(&ev, 0, sizeof(ev));
        ev.type = EV_KEY; ev.code = BTN_SOUTH; ev.value = 0;
        write(ufd, &ev, sizeof(ev));
        memset(&ev, 0, sizeof(ev));
        ev.type = EV_SYN; ev.code = SYN_REPORT; ev.value = 0;
        write(ufd, &ev, sizeof(ev));
        usleep(50000);

        // Read the release
        int got_release = 0;
        for (int i = 0; i < 20; i++) {
            ssize_t n = read(evfd, &read_ev, sizeof(read_ev));
            if (n != (ssize_t)sizeof(read_ev)) break;
            if (read_ev.type == EV_KEY && read_ev.code == BTN_SOUTH && read_ev.value == 0) {
                got_release = 1; break;
            }
        }
        printf("RELEASE_READ=%d\n", got_release);

        // Test rumble: upload FF_RUMBLE effect
        struct ff_effect eff;
        memset(&eff, 0, sizeof(eff));
        eff.type = FF_RUMBLE;
        eff.id = -1;
        eff.u.rumble.strong_magnitude = 32768;
        eff.u.rumble.weak_magnitude = 16384;
        int ff_ret = ioctl(ufd, EVIOCSFF, &eff);
        printf("RUMBLE_UPLOAD=%d\n", ff_ret >= 0 ? 1 : 0);
        if (ff_ret >= 0) {
            memset(&ev, 0, sizeof(ev));
            ev.type = EV_FF; ev.code = eff.id; ev.value = 1;
            write(ufd, &ev, sizeof(ev));
            printf("RUMBLE_PLAY=1\n");
            // Stop
            memset(&ev, 0, sizeof(ev));
            ev.type = EV_FF; ev.code = eff.id; ev.value = 0;
            write(ufd, &ev, sizeof(ev));
        }

        // Full FF roundtrip: upload on the event node + ack service here
        printf("pre-ff ufd F_GETFD=%d\n", fcntl(ufd, F_GETFD));
        ff_mode(ufd, evfd);

        // Cleanup
        ioctl(ufd, UI_DEV_DESTROY);
        close(ufd);
        close(evfd);

        int ok = got_press && got_release;
        printf("%s\n", ok ? "PASS: gamepad_e2e" : "FAIL: gamepad_e2e");
        return ok ? 0 : 1;
    }
    if (strcmp(argv[1], "create") == 0 && argc >= 5) {
        int fd = create_pad(argv[2], (uint16_t)strtoul(argv[3], NULL, 0), (uint16_t)strtoul(argv[4], NULL, 0));
        if (fd < 0) return 1;
        printf("%d\n", fd);
        return 0;
    }
    if (strcmp(argv[1], "inject") == 0 && argc >= 6) {
        int fd = atoi(argv[2]);
        struct input_event ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = (uint16_t)strtoul(argv[3], NULL, 0);
        ev.code = (uint16_t)strtoul(argv[4], NULL, 0);
        ev.value = (int32_t)strtol(argv[5], NULL, 0);
        write(fd, &ev, sizeof(ev));
        memset(&ev, 0, sizeof(ev));
        ev.type = EV_SYN; ev.code = SYN_REPORT;
        write(fd, &ev, sizeof(ev));
        return 0;
    }
    if (strcmp(argv[1], "destroy") == 0 && argc >= 3) {
        int fd = atoi(argv[2]);
        ioctl(fd, UI_DEV_DESTROY);
        close(fd);
        return 0;
    }
    return 1;
}
