#include "mavlink_reader.h"
#include "dataflash_logger.h"
#include "heart.h"

#include <errno.h>
#include <stdio.h> // for perror
#include <stdlib.h> // for abort
#include <sys/mman.h> // for MCL_ macros and mlockall
#include <syslog.h>

#include <unistd.h> // for usleep

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <signal.h>

static bool sighup_received = false;
void sighup_handler(int signal)
{
    sighup_received = true;
}

/* This is used to prevent swamping the log with error messages if
   something unexpected happens.
   Returns -1 if we cannot log an error now, or returns the number of
   messages skipped due to rate limiting if we can, i.e. a return of
   2 means log, and we have skipped 2 messages due to rate limiting. */
int MAVLink_Reader::can_log_error()
{
    unsigned ret_val;
    uint64_t now_us = clock_gettime_us(CLOCK_MONOTONIC);
    if ((now_us - err_time_us) < err_interval_us) {
        /* can't log */
        err_skipped++;
        return -1;
    }
    /* yes; say we can and set err_time_us assuming we do log something */
    err_time_us = now_us;
    ret_val = err_skipped;
    err_skipped = 0;
    return ret_val;
}


/*
* create_and_bind - create a socket and bind it to a local UDP port
*
* Used to create the socket on the upstream side that receives from and sends
* to telem_forwarder
*
* Returns fd on success, -1 on error.
*/
int MAVLink_Reader::create_and_bind()
{
    int fd;
    struct sockaddr_in sa;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("socket");
        abort();
    }

    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    sa.sin_port = 0; // we don't care what our port is

    if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        perror("bind");
        abort();
    }

    return fd;
} /* create_and_bind */


void MAVLink_Reader::pack_telem_forwarder_sockaddr(INIReader config)
{
    uint16_t tf_port = config.GetInteger("solo", "telem_forward_port", 14560);
    std::string ip = config.Get("solo", "soloIp", "10.1.1.10");

    memset(&sa_tf, 0, sizeof(sa_tf));
    sa_tf.sin_family = AF_INET;
    //    sa_tf.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    inet_aton(ip.c_str(), &sa_tf.sin_addr); // useful for debugging
    sa_tf.sin_port = htons(tf_port);
}

void MAVLink_Reader::instantiate_message_handlers(INIReader config)
{
    if (MAX_MESSAGE_HANDLERS - next_message_handler < 2) {
	syslog(LOG_INFO, "Insufficient message handler slots?!");
	exit(1);
    }

    DataFlash_Logger *dataflash_logger = new DataFlash_Logger(fd_telem_forwarder, sa_tf);
    if (dataflash_logger != NULL) {
	if (dataflash_logger->configure(config)) {
	    message_handler[next_message_handler++] = dataflash_logger;
	} else {
	    syslog(LOG_INFO, "Failed to configure dataflash logger");
	}
    } else {
	syslog(LOG_INFO, "Failed to create dataflash logger");
    }

    Heart *heart= new Heart(fd_telem_forwarder, sa_tf);
    if (heart != NULL) {
	if (heart->configure(config)) {
	    message_handler[next_message_handler++] = heart;
	} else {
	    syslog(LOG_INFO, "Failed to configure heart");
	}
    } else {
	syslog(LOG_INFO, "Failed to create heart");
    }
}

bool MAVLink_Reader::sane_telem_forwarder_packet(uint8_t *pkt, uint16_t pktlen)
{
    if (sa.sin_addr.s_addr != sa_tf.sin_addr.s_addr) {
	unsigned skipped;
	if ((skipped = can_log_error()) >= 0)
	    syslog(LOG_ERR, "[%u] received packet not from solo (0x%08x)",
		   skipped, sa.sin_addr.s_addr);
	return false;
    }
    if (pktlen < 8) { 
	unsigned skipped;
	if ((skipped = can_log_error()) >= 0)
	    syslog(LOG_ERR, "[%u] received runt packet (%d bytes)",
		   skipped, pktlen);
	return false;
    }
    if (pkt[0] != 254) {
	unsigned skipped;
	if ((skipped = can_log_error()) >= 0)
	    syslog(LOG_ERR, "[%u] received bad magic (0x%02x)",
		   skipped, pkt[0]);
	return false;
    }
    if (pkt[1] != (pktlen - 8)) {
	unsigned skipped;
	if ((skipped = can_log_error()) >= 0)
	    syslog(LOG_ERR, "[%u] inconsistent length (%u, %u)",
		   skipped, pkt[1], pktlen);
	return false;
    }
    return true;
}

void MAVLink_Reader::handle_telem_forwarder_recv()
{
    // ::printf("Receiving packet\n");
    /* packet from telem_forwarder */
    uint8_t pkt[TELEM_PKT_MAX];
    socklen_t sa_len = sizeof(sa);
    uint16_t res = recvfrom(fd_telem_forwarder, pkt, sizeof(pkt), 0, (struct sockaddr*)&sa, &sa_len);

    /* We get one mavlink packet per udp datagram. Sanity checks here
       are: must be from solo's IP and have a valid mavlink header. */
    if (!sane_telem_forwarder_packet(pkt, res)) {
	return;
    }

    /* packet is from solo and passes sanity checks */
    for(int i=0; i<next_message_handler; i++) {
	message_handler[i]->handle_packet(pkt, res);
    }
}

void MAVLink_Reader::do_idle_callbacks() {
    uint64_t now_us = clock_gettime_us(CLOCK_MONOTONIC);
    if (next_100hz_time <= now_us) {
	for(int i=0; i<next_message_handler; i++) {
	    message_handler[i]->idle_100Hz();
	}
	next_100hz_time += 10000;
    }
    if (next_10hz_time <= now_us) {
	for(int i=0; i<next_message_handler; i++) {
	    message_handler[i]->idle_10Hz();
	}
	next_10hz_time += 100000;
    }
    if (next_1hz_time <= now_us) {
	for(int i=0; i<next_message_handler; i++) {
	    message_handler[i]->idle_1Hz();
	}
	next_1hz_time += 1000000;
    }
    if (next_tenthhz_time <= now_us) {
	for(int i=0; i<next_message_handler; i++) {
	    message_handler[i]->idle_tenthHz();
	}
	next_tenthhz_time += 10000000;
    }
}

void MAVLink_Reader::run()
{
    openlog("dl", LOG_NDELAY, LOG_LOCAL1);

    syslog(LOG_INFO, "dataflash_logger starting: built " __DATE__ " " __TIME__);
    signal(SIGHUP, sighup_handler);

    INIReader config("/etc/sololink.conf");
    if (config.ParseError() < 0) {
        syslog(LOG_CRIT, "can't parse /etc/sololink.conf");
        exit(1);
    }

    /* prepare sockaddr used to contact telem_forwarder */
    pack_telem_forwarder_sockaddr(config);

    /* Prepare a port to receive and send data to/from telem_forwarder */
    /* does not return on failure */
    fd_telem_forwarder = create_and_bind();

    instantiate_message_handlers(config);

    while (1) {
	if (sighup_received) {
	    for(int i=0; i<next_message_handler; i++) {
		message_handler[i]->sighup_received();
	    }
	    sighup_received = false;
	}
        /* Wait for a packet, or time out if no packets arrive so we always
           periodically log status and check for new destinations. Downlink
           packets are on the order of 100/sec, so the timeout is such that
           we don't expect timeouts unless solo stops sending packets. We
           almost always get a packet with a 200 msec timeout, but not with
           a 100 msec timeout. (Timeouts don't really matter though.) */

	struct timeval timeout;

        fd_set fds;
        fd_set fds_err;
        FD_ZERO(&fds);
        uint8_t nfds = 0;
        FD_SET(fd_telem_forwarder, &fds);
        if (fd_telem_forwarder >= nfds)
            nfds = fd_telem_forwarder + 1;
	fds_err = fds;

        timeout.tv_sec = 0;
        timeout.tv_usec = 200000;
        int res = select(nfds, &fds, NULL, &fds_err, &timeout);

        if (res < 0) {
            unsigned skipped;
            if ((skipped = can_log_error()) >= 0)
                syslog(LOG_ERR, "[%u] select: %s", skipped, strerror(errno));
            /* this sleep is to avoid soaking the CPU if select starts
               returning immediately for some reason */
	    /* previous code was not checking errfds; we are now, so
	       perhaps this usleep can go away -pb20150730 */
            usleep(10000);
            continue;
        }

        if (res == 0) {
	  // select timeout
        }

        /* check for packets from telem_forwarder */
        if (FD_ISSET(fd_telem_forwarder, &fds_err)) {
            unsigned skipped;
            if ((skipped = can_log_error()) >= 0)
                syslog(LOG_ERR, "[%u] select(fd_telem_forwarder): %s", skipped, strerror(errno));
	}

        if (FD_ISSET(fd_telem_forwarder, &fds)) {
   	    handle_telem_forwarder_recv();
        }

	do_idle_callbacks();
    } /* while (1) */

} /* main */


/*
* main - entry point
*/
int main(int argc, char* argv[])
{
    MAVLink_Reader reader;
    reader.run();
}