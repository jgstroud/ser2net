/*
 *  ser2net - A program for allowing telnet connection to serial ports
 *  Copyright (C) 2020  Corey Minyard <minyard@acm.org>
 *
 *  SPDX-License-Identifier: GPL-2.0-only
 *
 *  In addition, as a special exception, the copyright holders of
 *  ser2net give you permission to combine ser2net with free software
 *  programs or libraries that are released under the GNU LGPL and
 *  with code included in the standard release of OpenSSL under the
 *  OpenSSL license (or modified versions of such code, with unchanged
 *  license). You may copy and distribute such a system following the
 *  terms of the GNU GPL for ser2net and the licenses of the other code
 *  concerned, provided that you include the source code of that
 *  other code when and as the GNU GPL requires distribution of source
 *  code.
 *
 *  Note that people who make modified versions of ser2net are not
 *  obligated to grant this special exception for their modified
 *  versions; it is their choice whether to do so. The GNU General
 *  Public License gives permission to release a modified version
 *  without this exception; this exception also makes it possible to
 *  release a modified version which carries forward this exception.
 */

#ifndef PORT
#define PORT

#ifdef WIN32
#include <ws2tcpip.h>
#else
#include <netdb.h>
#endif

#include <gensio/gensio.h>

#include "gbuf.h"
#include "absout.h"
#include "mdns.h"
#include "timeproc.h"
#include "fileio.h"

/* States for the net_to_dev_state and dev_to_net_state. */
#define PORT_NOT_STARTED		0 /* The dataxfer_start_port failed. */
#define PORT_CLOSED			1 /* The accepter is disabled. */
#define PORT_UNCONNECTED		2 /* The TCP port is not connected
                                             to anything right now. */
#define PORT_WAITING_INPUT		3 /* Waiting for input from the
					     input side. */
#define PORT_WAITING_OUTPUT_CLEAR	4 /* Waiting for output to clear
					     so I can send data. */
#define PORT_CLOSING			5 /* Waiting for output close
					     string to be sent. */
typedef struct trace_info_s
{
    bool hexdump;     /* output each block as a hexdump */
    bool timestamp;   /* precede each line with a timestamp */
    char *filename;   /* open file.  NULL if not used */
    ftype *f;         /* open file.  NULL if not used */
} trace_info_t;

typedef struct port_info port_info_t;
typedef struct net_info net_info_t;

struct net_info {
    port_info_t	   *port;		/* My port. */

    bool	   closing;		/* Is the connection in the process
					   of closing? */

    struct gensio   *net;		/* When connected, the network
					   connection, NULL otherwise. */

    bool remote_fixed;			/* Tells if the remote address was
					   set in the configuration, and
					   cannot be changed. */
    bool connect_back;			/* True if we connect to the remote
					   address when data comes in. */
    const char *remote_str;

    gensiods bytes_received;		/* Number of bytes read from the
					   network port. */
    gensiods bytes_sent;		/* Number of bytes written to the
					   network port. */

    /*
     * These are used for timing out the port if there is no activity.
     */
    gensiods last_bytes_received;
    gensiods last_bytes_sent;
    gensiods last_send_queue_len;

    struct gbuf *banner;		/* Outgoing banner */

    gensiods write_pos;			/* Our current position in the
					   output buffer where we need
					   to start writing next. */

    int            timeout_left;	/* The amount of time left (in
					   seconds) before the timeout
					   goes off. */

    bool timeout_running;		/* Do we use the timer? */

    /*
     * Close the session when all the output has been written to the
     * network port.
     */
    bool close_on_output_done;

    unsigned char linestate_mask;
    unsigned char modemstate_mask;
    bool modemstate_sent;	/* Has a modemstate been sent? */
    bool linestate_sent;	/* Has a linestate been sent? */

    char remaddr[NI_MAXHOST + NI_MAXSERV + 2];

    /*
     * If a user gets kicked, store the information for the new user
     * here since we have already accepted the connection or received
     * the packet, we have to store it someplace.
     */
    struct gensio *new_net;
};

struct port_info
{
    struct gensio_lock *lock;

    /* If false, port is not accepting, if true it is. */
    bool enabled;

    const char *shutdown_reason;

    void (*port_op_done)(struct port_info *, void *);
    void *port_op_data;

    /* FIXME - remove this with old config.  An old config specified telnet. */
    bool do_telnet;

    /* Keeps a count of retried port startups. */
    unsigned int retry_startup_counter;

    /* The port has been deleted, but still has connections in use. */
    bool deleted;

    /* Used to count operations (timer stops) during free. */
    unsigned int free_count;

    int            timeout;		/* The number of seconds to
					   wait without any I/O before
					   we shut the port down. */

    /*
     * Take the OS queue into account when timing out a connection.
     */
    bool timeout_on_os_queue;

    struct gensio_timer *timer;		/* Used to timeout when the no
					   I/O has been seen for a
					   certain period of time. */

    struct gensio_timer *send_timer;	/* Used to delay a bit when
					   waiting for characters to
					   batch up as many characters
					   as possible. */
    bool send_timer_running;

    /* Time to retry if the connector/accepter fails to come up. */
    unsigned int connector_retry_time;
    unsigned int accepter_retry_time;

    unsigned int nocon_read_enable_time_left;
    /* Used if a connect back is requested an no connections could
       be made, to try again. */

    /*
     * Used to count timeouts during a shutdown, to make sure close
     * happens in a reasonable amount of time.  If this is zero, this
     * means that shutdown_port_io() has already been called.
     */
    unsigned int shutdown_timeout_count;

    struct gensio_runner *runshutdown;	/* Used to run things at the
					   base context.  This way we
					   don't have to worry that we
					   are running inside a
					   handler context that needs
					   to be waited for exit. */

    unsigned int chardelay;             /* The amount of time to wait after
					   receiving a character before
					   sending it, unless we receive
					   another character.  Based on
					   bit rate. */

    unsigned int bps;			/* Bits per second rate. */
    unsigned int bpc;			/* Bits per character. */
    unsigned int stopbits;
    unsigned int paritybits;

    bool enable_chardelay;

    /* Disable data flowing in the given directions. */
    bool no_dev_to_net;
    bool no_net_to_dev;

    unsigned int chardelay_scale;	/* The number of character
					   periods to wait for the
					   next character, in tenths of
					   a character period. */
    unsigned int chardelay_min;		/* The minimum chardelay, in
					   microseconds. */
    unsigned int chardelay_max;		/* Maximum amount of time to
					   wait before sending the data. */
    gensio_time send_time;		/* When using chardelay, the
					   time when we will send the
					   data, no matter what, set
					   by chardelay_max. */

    /* Information about the network port. */
    char               *name;           /* The name given for the port. */
    char               *accstr;         /* The accepter string. */
    struct gensio_accepter *accepter;	/* Used to receive new connections. */
    bool accepter_stopped;

    struct port_remaddr *remaddrs;	/* Remote addresses allowed. */
    struct port_remaddr *connbacks;	/* Connect back addresses */
    unsigned int num_waiting_connect_backs;
    unsigned int connback_timeout;
    bool connback_timeout_set;

    unsigned int max_connections;	/* Maximum number of connections
					   we can accept at a time for this
					   port. */
    net_info_t *netcons;

    gensiods dev_bytes_received;    /* Number of bytes read from the device. */
    gensiods dev_bytes_sent;        /* Number of bytes written to the device. */

    /*
     * Informationd use when transferring information from the network
     * port to the terminal device.
     */
    int            net_to_dev_state;		/* State of transferring
						   data from the network port
                                                   to the device. */

    struct gbuf    net_to_dev;			/* Buffer for network
						   to dev transfers. */
    struct controller_info *net_monitor; /* If non-null, send any input
					    received from the network port
					    to this controller port. */
    struct gbuf *devstr;		 /* Outgoing string */

    /*
     * Information used when transferring information from the
     * terminal device to the network port.
     */
    int            dev_to_net_state;		/* State of transferring
						   data from the device to
                                                   the network port. */

    struct gbuf dev_to_net;

    /*
     * We have called shutdown_port but the accepter has not yet been
     * read disabled.
     */
    bool shutdown_started;

    struct controller_info *dev_monitor; /* If non-null, send any input
					    received from the device
					    to this controller port. */

    struct port_info *next;		/* Used to keep a linked list
					   of these. */

    /*
     * The port was reconfigured but had pending users.  This holds the
     * new config until the pending users have finished.
     */
    struct port_info *new_config;

    char *rs485; /* If not NULL, rs485 was specified. */

    /* For RFC 2217 */
    unsigned char last_modemstate;
    unsigned char last_linestate;

    /* Allow RFC 2217 mode */
    bool allow_2217;

    /* Send a break if we get a sync command? */
    bool telnet_brk_on_sync;

    /* kickolduser mode */
    bool kickolduser_mode;

    /* Banner to display at startup, or NULL if none. */
    char *bannerstr;

    /* RFC 2217 signature. */
    char *signaturestr;

    /* String to send to device at startup, or NULL if none. */
    char *openstr;

    /* String to send to device at close, or NULL if none. */
    char *closestr;

    /*
     * Close on string to shutdown connection when received from
     * serial side, or NULL if none.
     */
    char *closeon;
    gensiods closeon_pos;
    gensiods closeon_len;

    /*
     * File to read/write trace, NULL if none.  If the same, then
     * trace information is in the same file, only one open is done.
     */
    trace_info_t trace_read;
    trace_info_t trace_write;
    trace_info_t trace_both;

    /*
     * Pointers to the above, that way if two are the same file we can just
     * set up one and point both to it.
     */
    trace_info_t *tr;
    trace_info_t *tw;
    trace_info_t *tb;

    char *devname;
    struct gensio *io; /* For handling I/O operation to the device */
    bool io_open;
    void (*dev_write_handler)(port_info_t *);

    /*
     * devname as specified on the line, not the substituted version.  Only
     * non-null if devname was substituted.
     */
    char *orig_devname;

    /*
     * LED to flash for serial traffic
     */
    struct led_s *led_tx;
    struct led_s *led_rx;
    /*
     * LED to light on connect
     */
    struct led_s *led_conn;

    /*
     * Directory that has authentication info.
     */
    char *authdir;

    /*
     * Enable PAM authentication
     */
    char *pamauth;

    /*
     * List of authorized users.  If NULL, all users are authorized.
     * If no allowed users are specified, the default is taken.
     */
    struct gensio_list *allowed_users;
    char *default_allowed_users;

    /*
     * Delimiter for sending.
     */
    char *sendon;
    gensiods sendon_pos;
    gensiods sendon_len;

#ifdef DO_MDNS
    struct mdns_info mdns_info;
#endif
};

/* In dataxfer.c */
void handle_new_net(port_info_t *port, struct gensio *net, net_info_t *netcon);
int handle_dev_event(struct gensio *io, void *user_data, int event, int err,
		     unsigned char *buf, gensiods *buflen,
		     const char *const *auxdata);
int port_dev_enable(port_info_t *port);
int gbuf_write(port_info_t *port, struct gbuf *buf);
void report_disconnect(port_info_t *port, net_info_t *netcon);
void port_send_timeout(struct gensio_timer *timer, void *data);

/* In port.c */
extern struct gensio_lock *ports_lock;
extern port_info_t *ports;
extern port_info_t *new_ports;
extern port_info_t *new_ports_end;
net_info_t *first_live_net_con(port_info_t *port);
bool port_in_use(port_info_t *port);
int is_device_already_inuse(port_info_t *check_port);
int num_connected_net(port_info_t *port);
gensiods net_raddr(struct gensio *io, struct sockaddr_storage *addr,
		   gensiods *socklen);
void reset_timer(net_info_t *netcon);
#define for_each_connection(port, netcon)			\
    for (netcon = port->netcons;				\
	 netcon < &(port->netcons[port->max_connections]);	\
	 netcon++)
void shutdown_one_netcon(net_info_t *netcon, const char *reason);
int dataxfer_setup_port(port_info_t *new_port, struct absout *eout);
int startup_port(struct absout *eout, port_info_t *port);
int shutdown_port(port_info_t *port, const char *errreason);
void port_start_timer(port_info_t *port);

/* In portconfig.c */
bool remaddr_check(const struct port_remaddr *list,
		   const struct sockaddr *addr, socklen_t len);
void remaddr_list_free(struct port_remaddr *list);
void free_port(port_info_t *port);

/* In ser2net_str.c */
struct gbuf *process_str_to_buf(port_info_t *port, net_info_t *netcon,
				const char *str, struct absout *eout);
char *process_str_to_str(port_info_t *port, net_info_t *netcon,
			 const char *str, timev *ts,
			 gensiods *lenrv, int isfilename, struct absout *eout);
gensiods net_raddr_str(struct gensio *io, char *buf, gensiods buflen);

/* In rotator.c */
void shutdown_rotators(void);
int init_rotators(void);

/* In trace.c */
void header_trace(port_info_t *port, net_info_t *netcon);
void footer_trace(port_info_t *port, char *type, const char *reason);
void do_trace(port_info_t *port, trace_info_t *t, const unsigned char *buf,
	      gensiods buf_len, const char *prefix);
void setup_trace(port_info_t *port, struct absout *eout);
void shutdown_trace(port_info_t *port);

/* In addsyattrs.c */
void add_sys_attrs(struct absout *eout, const char *portname,
		   const char *devname,
		   const char ***txt, gensiods *args, gensiods *argc);

#endif /* PORT */
