// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * edfinfo - read information from electricity meter (France)
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <mosquitto.h>
#include <errno.h>

#include "log.h"
#include "edfinfo.h"
#include "frame.h"
#include "backend.h"
#include "stats.h"

static struct mqtt_config {
	const char	*host;
	int		port;
	int		keepalive;
	const char	*id;
	const char	*topic;
	int             ratelimit;
	int             threshold;
} mqtt_config = {
	.host		= "localhost",
	.port		= 1883,
	.keepalive	= 60,
	.id		= NULL,
	.topic		= "sensors/power/edfinfo",
	.ratelimit	= 60, /* Seconds */
	.threshold	= 20, /* Watts */
};

#define MATCH(n) (strcmp(name, n) == 0)

static int mqtt_configure(const char *name, const char *value)
{
	if (MATCH("host")) {
		mqtt_config.host = strdup(value);
	} else if (MATCH("port")) {
		mqtt_config.port = atoi(value);
	} else if (MATCH("keepalive")) {
		mqtt_config.keepalive = atoi(value);
	} else if (MATCH("topic")) {
		mqtt_config.topic = strdup(value);
	} else if (MATCH("ratelimit")) {
		mqtt_config.ratelimit = atoi(value);
	} else if (MATCH("threshold")) {
		mqtt_config.threshold = atoi(value);
	} else {
		fprintf(stderr, "unknown config name mqtt/%s\n", name);
		return 0;  /* unknown section/name, error */
	}

	/* success */
	return 1;
}

static struct mosquitto *mqtt_broker;
static bool mqtt_connected;

/*
 * Callbacks are handled in a different thread (with a mutex). See
 * mosquitto implementation.
 */
static void on_connect(struct mosquitto *mosq __unused, void *data __unused,
		       int rc __unused)
{
	if (rc) {
		ERROR("mqtt: unable to connect to broker %s",
		      mosquitto_connack_string(rc));
		return;
	}
	NOTICE("mqtt: %s connected to broker %s", mqtt_config.id,
	       mqtt_config.host);
	mqtt_connected = true;
}

static void on_disconnect(struct mosquitto *mosq __unused, void *data __unused,
			  int rc __unused)
{
	WARN("mqtt: %s disconnected from broker", mqtt_config.id);
	mqtt_connected = false;
}

static void on_publish(struct mosquitto *mosq __unused, void *data __unused,
		       int mid)
{
	INFO("mqtt: sent message #%d", mid);
}

static char default_mqttid[64];

static int mqtt_init(void)
{
	bool clean_session = true;
	struct mosquitto *mosq = NULL;

	mosquitto_lib_init();

	if (!mqtt_config.id) {
		int n = snprintf(default_mqttid, sizeof(default_mqttid),
				 "%s/%d-", progname, getpid());
		gethostname(default_mqttid + n, sizeof(default_mqttid) - n);
		mqtt_config.id = default_mqttid;
	}

	mosq = mosquitto_new(mqtt_config.id, clean_session, NULL);
	if (!mosq) {
		ERROR("mqtt: failed to allocate connection: %s",
		      strerror(errno));
		return -1;
	}

	mosquitto_connect_callback_set(mosq, on_connect);
	mosquitto_disconnect_callback_set(mosq, on_disconnect);
	mosquitto_publish_callback_set(mosq, on_publish);

	mqtt_broker = mosq;
	return 0;
}

static void mqtt_fini(void)
{
	if (mqtt_broker) {
		mosquitto_disconnect(mqtt_broker);
		mosquitto_loop_stop(mqtt_broker, true /* force disconnect */);
		mosquitto_destroy(mqtt_broker);
		mqtt_broker = NULL;
	}
	mosquitto_lib_cleanup();
}

static int mqtt_connect_loop(struct mosquitto *mosq)
{
	static bool loop_started;
	int ret = 0;

	if (loop_started)
		return 0;

	ret = mosquitto_connect(mosq, mqtt_config.host, mqtt_config.port,
				mqtt_config.keepalive);
	if (ret) {
		static bool warn_once;

		if (!warn_once) {
			ERROR("mqtt: unable to connect to broker %s : %s",
			      mqtt_config.host, mosquitto_strerror(ret));
			warn_once = true;
		}
		return ret;
	}

	ret = mosquitto_loop_start(mosq);
	if (ret) {
		ERROR("mqtt: unable to start loop : %s",
		      mosquitto_strerror(ret));
		return ret;
	}
	loop_started = true;
	return 0;
}

static int check_ratelimit(int ratelimit)
{
	static struct timeval prev;
	struct timeval now;

	gettimeofday(&now, NULL);
	if (now.tv_sec - prev.tv_sec < ratelimit)
		return 0;

	prev = now;
	return 1;
}

static int mqtt_publish(const char *topic, const char *msg, size_t len)
{
	int ret = 0;
	int msg_id;

	ret = mosquitto_publish(mqtt_broker, &msg_id, topic, len, msg, 0, false);
	if (ret) {
		ERROR("mqtt: publish failed %d %s", ret,
		      mosquitto_strerror(ret));
		mosquitto_disconnect(mqtt_broker);
	}
	return ret;
}

/*
 * frame filtering depending on power variations. This drops ~90% of
 * frames.
 */
static int filter_frame(const struct frame *frame)
{
	static unsigned int prev_power;

	bool same = prev_power == frame->power;

	prev_power = frame->power;

	return same;
}

static int mqtt_push(const struct frame *frame)
{
	char topic[64];
	char msg[32];
	int ret = 0;

	if (!mqtt_connected) {
		/* Initialize connection loop if needed and bail out */
		mqtt_connect_loop(mqtt_broker);
		return 0;
	}

	if (check_ratelimit(mqtt_config.ratelimit)) {
		/* Publish Index */
		snprintf(topic, sizeof(topic), "%s/index", mqtt_config.topic);
		ret = snprintf(msg, sizeof(msg), "%d", frame->energy);
		DEBUG("mqtt: msg size=%d \"%s\"", ret, msg);

		ret = mqtt_publish(topic, msg, ret);
		if (ret)
			goto out;

		/* Publish power average values */
		snprintf(topic, sizeof(topic), "%s/average", mqtt_config.topic);
		ret = snprintf(msg, sizeof(msg), "%d/%d/%d",
			       frame_stack_average(1 * 60),
			       frame_stack_average(5 * 60),
			       frame_stack_average(30 * 60));
		DEBUG("mqtt: msg size=%d \"%s\"", ret, msg);

		ret = mqtt_publish(topic, msg, ret);
		if (ret)
			goto out;
	}

	/* Publish Current Power */
	if (filter_frame(frame)) {
		INFO("discarding frame with power %d Watts", frame->power);
		stats.mqtt_dropped++;
		return 0;
	}

	snprintf(topic, sizeof(topic), "%s/power", mqtt_config.topic);
	ret = snprintf(msg, sizeof(msg), "%d", frame->power);
	DEBUG("mqtt: msg size=%d \"%s\"", ret, msg);

	ret = mqtt_publish(topic, msg, ret);
	if (ret)
		goto out;
	stats.mqtt_pushed++;
out:
	return ret;
}

static struct backend_ops mqtt_ops = {
	.configure = mqtt_configure,
	.init = mqtt_init,
	.push = mqtt_push,
	.fini = mqtt_fini,
};

backend_register("mqtt", &mqtt_ops)
