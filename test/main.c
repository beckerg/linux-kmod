/*
 * Copyright (c) 2016-2017 Greg Becker.  All rights reserved.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/param.h>

const char *progname;

int
main(int argc, char **argv)
{
    size_t devsz, devbufsz, rndbufsz, len, off;
    const char *rndname = "/dev/urandom";
    char *devbuf, *rndbuf;
    const char *devname;
    char *devaddr, *pc;
    int devfd, rndfd;
    ssize_t cc;
    int rc;

    progname = argv[0];

    if (argc < 3) {
        fprintf(stderr, "usage: %s <file> <length>\n", progname);
        exit(1);
    }

    devname = argv[1];
    devsz = strtoul(argv[2], NULL, 0);

    rndbufsz = 8195;
    rndbuf = malloc(rndbufsz * 2);
    if (!rndbuf) {
        fprintf(stderr, "%s: unable to malloc rndbuf %zu\n", progname, rndbufsz);
        exit(1);
    }

    rndfd = open(rndname, O_RDONLY);
    if (-1 == rndfd) {
        fprintf(stderr, "%s; open(%s): %s\n", progname, rndname, strerror(errno));
        exit(1);
    }

    cc = read(rndfd, rndbuf, rndbufsz * 2);
    if (cc != rndbufsz * 2) {
        fprintf(stderr, "%s; read %d len=%zu cc=%ld: %s\n",
                progname, rndfd, rndbufsz * 2, cc, strerror(errno));
    }

    devbufsz = 1024 * 1024;
    devbuf = malloc(devbufsz);
    if (!devbuf) {
        fprintf(stderr, "%s: unable to malloc devbuf %zu\n", progname, devbufsz);
        exit(1);
    }

    devfd = open(devname, O_RDWR);
    if (-1 == devfd) {
        fprintf(stderr, "%s; open(%s): %s\n", progname, devname, strerror(errno));
        exit(1);
    }

    for (off = 0; off < devsz; ) {
        pc = rndbuf + (off % (rndbufsz / 2));
        len = MIN(devsz - off, rndbufsz);

        cc = pwrite(devfd, pc, len, off);
        if (cc != len) {
            fprintf(stderr, "%s; write %d len=%zu cc=%ld: %s\n",
                    progname, devfd, len, cc, strerror(errno));
            exit(1);
        }

        off += len;
    }

    for (off = 0; off < devsz; ) {
        pc = rndbuf + (off % (rndbufsz / 2));
        len = MIN(devsz - off, rndbufsz);

        cc = pread(devfd, devbuf, len, off);
        if (cc != len) {
            fprintf(stderr, "%s; read %d len=%zu cc=%ld: %s\n",
                    progname, devfd, len, cc, strerror(errno));
            exit(1);
        }

        if (0 != memcmp(devbuf, pc, len)) {
            fprintf(stderr, "%s; verify read failed: off=%zu len=%zu\n",
                    progname, off, len);
            exit(1);
        }

        off += len;
    }

    devaddr = mmap(NULL, devsz, PROT_READ | PROT_WRITE, MAP_SHARED, devfd, 0);
    if (MAP_FAILED == devaddr) {
        fprintf(stderr, "%s; mmap(%d): %s\n", progname, devfd, strerror(errno));
        exit(1);
    }

    close(devfd);

    for (off = 0; off < devsz; ) {
        pc = rndbuf + (off % (rndbufsz / 2));
        len = MIN(devsz - off, rndbufsz);

        if (0 != memcmp(devaddr + off, pc, len)) {
            fprintf(stderr, "%s; verify mmap 1 failed: off=%zu len=%zu\n",
                    progname, off, len);
            exit(1);
        }

        off += len;
    }

    rc = munmap(devaddr, devsz);
    if (rc) {
        fprintf(stderr, "%s; munmap(%p, %zu): %s\n",
                progname, devaddr, devsz, strerror(errno));
        exit(1);
    }


    devfd = open(devname, O_RDWR);
    if (-1 == devfd) {
        fprintf(stderr, "%s; open(%s): %s\n", progname, devname, strerror(errno));
        exit(1);
    }

    devaddr = mmap(NULL, devsz, PROT_READ | PROT_WRITE,
                   MAP_SHARED | MAP_NORESERVE, devfd, 0);
    if (MAP_FAILED == devaddr) {
        fprintf(stderr, "%s; mmap(%d): %s\n", progname, devfd, strerror(errno));
        exit(1);
    }

    close(devfd);

    for (off = 0; off < devsz; ) {
        pc = rndbuf + (off % (rndbufsz / 2));
        len = MIN(devsz - off, rndbufsz);

        if (0 != memcmp(devaddr + off, pc, len)) {
            fprintf(stderr, "%s; verify mmap 2 failed: off=%zu len=%zu\n",
                    progname, off, len);
            exit(1);
        }

        off += len;
    }

    return 0;
}
