#include "nng/nng.h"
#include "core/nng_impl.h"
#include "nng/exchange/exchange_client.h"
#include "nng/exchange/exchange.h"
#include "core/defs.h"
#include <nuts.h>

#define UNUSED(x) ((void) x)

static inline void free_msg_list(nng_msg **msgList, nng_msg *msg, int *lenp, int freeMsg)
{
	for (int i = 0; i < *lenp; i++) {
		if (freeMsg) {
			nng_msg_free(msgList[i]);
		}
	}

	if (msg != NULL) {
		nng_msg_free(msg);
	}
	if (msgList != NULL) {
		nng_free(msgList, sizeof(nng_msg *) * (*lenp));
	}
	if (lenp != NULL) {
		nng_free(lenp, sizeof(int));
	}
}

static inline void client_get_msgs(nng_socket sock, uint64_t key, int count, int *lenp, nng_msg ***msgList)
{
	nni_aio *aio = NULL;
	NUTS_PASS(nng_aio_alloc(&aio, NULL, NULL));

	nng_msg *msg;
	nng_msg_alloc(&msg, 0);
	nng_msg_set_timestamp(msg, key);
	nni_msg_set_proto_data(msg, NULL, (void *)(uintptr_t)count);

	nni_aio_set_msg(aio, msg);

	nng_recv_aio(sock, aio);
	nng_aio_wait(aio);

	*msgList = (nng_msg **)nng_aio_get_msg(aio);
	*lenp = (uintptr_t)nng_aio_get_prov_data(aio);

	nng_msg_free(msg);
	nng_aio_free(aio);
}

//
// Publish a message to the given topic and with the given QoS.
void
client_publish(nng_socket sock, const char *topic, uint64_t key, uint8_t *payload,
    uint32_t payload_len, uint8_t qos, bool verbose)
{
	UNUSED(verbose);
	// create a PUBLISH message
	nng_msg *pubmsg;
	nng_mqtt_msg_alloc(&pubmsg, 0);

	uint8_t *header = nng_msg_header(pubmsg);
	*header = *header | CMD_PUBLISH;
	nng_mqtt_msg_set_packet_type(pubmsg, NNG_MQTT_PUBLISH);
	nng_mqtt_msg_set_publish_dup(pubmsg, 0);
	nng_mqtt_msg_set_publish_qos(pubmsg, qos);
	nng_mqtt_msg_set_publish_retain(pubmsg, 0);
	nng_mqtt_msg_set_publish_payload(
	    pubmsg, (uint8_t *) payload, payload_len);
	nng_mqtt_msg_set_publish_topic(pubmsg, topic);
	nng_mqtt_msg_set_publish_topic_len(pubmsg, strlen(topic));

	nni_aio *aio = NULL;
	NUTS_PASS(nng_aio_alloc(&aio, NULL, NULL));


	nng_msg_set_timestamp(pubmsg, key);
	nni_aio_set_msg(aio, pubmsg);

	nng_send_aio(sock, aio);
	nng_aio_wait(aio);

	int *lenp = NULL;
	nng_msg **msgList = (nng_msg **)nng_aio_get_prov_data(aio);
	nng_msg *msg = nng_aio_get_msg(aio);
	if (msgList != NULL && msg != NULL) {
		lenp = nng_msg_get_proto_data(msg);
		free_msg_list(msgList, msg, lenp, 1);
	}

	nng_aio_free(aio);
}

void
test_exchange_client(void)
{
	int rv = 0;
	uint64_t key = 0;
	nng_socket sock;
	exchange_t *ex = NULL;

	NUTS_TRUE(nng_exchange_client_open(&sock) == 0);

	char **ringBufferName;
	ringBufferName = nng_alloc(1 * sizeof(char *));
	for (int i = 0; i < 1; i++) {
		ringBufferName[i] = nng_alloc(100 * sizeof(char));
	}

	strcpy(ringBufferName[0], "ringBuffer1");

	int caps = 10;

	NUTS_TRUE(exchange_init(&ex, "exchange1", "topic1", (void *)&caps, ringBufferName, 1) == 0);
	NUTS_TRUE(ex != NULL);

	for (int i = 0; i < 1; i++) {
		nng_free(ringBufferName[i], sizeof(*ringBufferName[i]));
	}
	nng_free(ringBufferName, sizeof(*ringBufferName));

	nng_socket_set_ptr(sock, NNG_OPT_EXCHANGE_BIND, ex);

	key = 0;
	client_publish(sock, "topic1", key, NULL, 0, 0, 0);

	nni_msg *msg = NULL;
	nni_sock *nsock = NULL;

	rv = nni_sock_find(&nsock, sock.id);
	NUTS_TRUE(rv == 0 && nsock != NULL);
	nni_sock_rele(nsock);

	rv = exchange_client_get_msg_by_key(nni_sock_proto_data(nsock), key, &msg);
	NUTS_TRUE(rv == 0 && msg != NULL);

	int *lenp;
	nng_msg **msgList = NULL;
	rv = exchange_client_get_msgs_by_key(nni_sock_proto_data(nsock), key, 1, &msgList);
	NUTS_TRUE(rv == 0 && msgList != NULL);
	lenp = nng_alloc(sizeof(int));
	*lenp = 1;
	free_msg_list(msgList, NULL, lenp, 0);
	msgList = NULL;

	/* Use aio recv to get msgs by key */
	lenp = nng_alloc(sizeof(int));
	*lenp = 0;
	client_get_msgs(sock, key, 1, lenp, &msgList);
	NUTS_TRUE(*lenp == 1 && msgList != NULL);
	free_msg_list(msgList, NULL, lenp, 0);

	/* Only one element in ringbuffer */
	msgList = NULL;
	rv = exchange_client_get_msgs_by_key(nni_sock_proto_data(nsock), key, 2, &msgList);
	NUTS_TRUE(rv == -1 && msgList == NULL);

	/* fuzz search start */
	lenp = nng_alloc(sizeof(int));
	rv = exchange_client_get_msgs_fuzz(nni_sock_proto_data(nsock), 0, 3, lenp, &msgList);
	NUTS_TRUE(rv == 0 && *lenp == 1 && msgList != NULL);
	free_msg_list(msgList, NULL, lenp, 0);

	msgList = NULL;
	int len = 0;
	rv = exchange_client_get_msgs_fuzz(nni_sock_proto_data(nsock), 2, 3, &len, &msgList);
	NUTS_TRUE(rv != 0 && len == 0 && msgList == NULL);
	/* fuzz search end */

	for (int i = 1; i < 10; i++) {
		key = i;
		client_publish(sock, "topic1", key, NULL, 0, 0, 0);
	}

	/* Use aio recv to get msgs by key */
	lenp = nng_alloc(sizeof(int));
	*lenp = 0;
	client_get_msgs(sock, key, 10, lenp, &msgList);
	NUTS_TRUE(*lenp == 10 && msgList != NULL);
	free_msg_list(msgList, NULL, lenp, 0);


	/* Ringbuffer is full and msgs returned need to free */
	client_publish(sock, "topic1", 10, NULL, 0, 0, 0);

	return;
}

NUTS_TESTS = {
	{ "Exchange client test", test_exchange_client },
	{ NULL, NULL },
};