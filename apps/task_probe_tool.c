#include "stm32f2xx_hal.h"
#include "cmsis_os.h"
#include "lwip.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "task_probe_tool.h"

extern struct netif gnetif;

static int broadcast_sock = -1;
static struct sockaddr_in broadcast_addr;

static int probe_server_sock = -1;

static struct sockaddr_in probe_client_address_in;
static struct sockaddr_in log_client_address_in;

static uint8_t log_client_address_valid = 0;

static char probe_recv_buffer[RECV_BUFFER_SIZE];
static char probe_send_buffer[SEND_BUFFER_SIZE];

static uint32_t select_timeout_count = 0;
static uint32_t select_timeout_count_stamp = 0;


static void *os_alloc(size_t size)
{
	return pvPortMalloc(size);
}

static void os_free(void *p)
{
	vPortFree(p);
}

static int log_server_sendto(void *data, size_t size)
{
	if(log_client_address_valid == 0) {
		return 0;
	}

	return sendto(probe_server_sock, data, size, 0, (struct sockaddr *)&log_client_address_in, sizeof(log_client_address_in));
}

int udp_log_printf(const char *fmt, ...)
{
	va_list ap;
	int size = 0;
	char *log_buffer = (char *)os_alloc(LOG_BUFFER_SIZE);

	if(log_buffer == NULL) {
		return size;
	}

	va_start(ap, fmt);
	size = vsnprintf(log_buffer, LOG_BUFFER_SIZE, fmt, ap);
	va_end(ap);

	if((LOG_BUFFER_SIZE - 1) <= size) {
		size = LOG_BUFFER_SIZE;
	} else {
		size += 1;
	}

	size = log_server_sendto(log_buffer, size);

	os_free(log_buffer);

	return (size - 1);
}

static int32_t my_isprint(int32_t c)
{
	if(((uint8_t)c >= 0x20) && ((uint8_t)c <= 0x7e)) {
		return 0x4000;
	} else {
		return 0;
	}
}

#define BUFFER_LEN 80
void udp_log_hexdump(const char *label, const char *data, int len)
{
	int ret = 0;
	char *buffer = (char *)os_alloc(BUFFER_LEN);
	const char *start = data;
	const char *end = start + len;
	int c;
	int puts(const char *s);
	char *buffer_start = buffer;
	int i;
	long offset = 0;
	int bytes_per_line = 16;

	if(buffer == NULL) {
		return;
	}

	if(label != NULL) {
		ret = snprintf(buffer, BUFFER_LEN, "%s:\n", label);

		if((BUFFER_LEN - 1) <= ret) {
			ret = BUFFER_LEN;
		} else {
			ret += 1;
		}

		ret = log_server_sendto(buffer, ret);
	}

	while(start < end) {
		int left = BUFFER_LEN - 1;//剩余可打印字符数,去掉结束的0
		long address = start - data;

		buffer_start = buffer;

		c = end - start;

		if(c > bytes_per_line) {
			c = bytes_per_line;
		}

		ret = snprintf(buffer_start, left + 1, "%08lx", offset + address);
		buffer_start += ret;

		if(left <= ret) {
			left = 0;
			goto out;
		} else {
			left -= ret;
		}

		ret = snprintf(buffer_start, left + 1, " ");
		buffer_start += ret;

		if(left <= ret) {
			left = 0;
			goto out;
		} else {
			left -= ret;
		}

		for(i = 0; i < c; i++) {
			if(i % 8 == 0) {
				ret = snprintf(buffer_start, left + 1, " ");
				buffer_start += ret;

				if(left <= ret) {
					left = 0;
					goto out;
				} else {
					left -= ret;
				}
			}

			ret = snprintf(buffer_start, left + 1, "%02x ", (unsigned char)start[i]);
			buffer_start += ret;

			if(left <= ret) {
				left = 0;
				goto out;
			} else {
				left -= ret;
			}
		}

		for(i = c; i < bytes_per_line; i++) {
			if(i % 8 == 0) {
				ret = snprintf(buffer_start, left + 1, " ");
				buffer_start += ret;

				if(left <= ret) {
					left = 0;
					goto out;
				} else {
					left -= ret;
				}
			}

			ret = snprintf(buffer_start, left + 1, "%2s ", " ");
			buffer_start += ret;

			if(left <= ret) {
				left = 0;
				goto out;
			} else {
				left -= ret;
			}
		}

		ret = snprintf(buffer_start, left + 1, "|");
		buffer_start += ret;

		if(left <= ret) {
			left = 0;
			goto out;
		} else {
			left -= ret;
		}

		for(i = 0; i < c; i++) {
			ret = snprintf(buffer_start, left + 1, "%c", my_isprint(start[i]) ? start[i] : '.');
			buffer_start += ret;

			if(left <= ret) {
				left = 0;
				goto out;
			} else {
				left -= ret;
			}
		}

		ret = snprintf(buffer_start, left + 1, "|");
		buffer_start += ret;

		if(left <= ret) {
			left = 0;
			goto out;
		} else {
			left -= ret;
		}

		ret = snprintf(buffer_start, left + 1, "\n");
		buffer_start += ret;

		if(left <= ret) {
			left = 0;
			goto out;
		} else {
			left -= ret;
		}

	out:
		ret = log_server_sendto(buffer, BUFFER_LEN - left);

		start += c;
	}

	os_free(buffer);
}

int udp_log_puts(const char *s)
{
	int ret = 0;
	ret = strlen(s);

	if(ret > (1024 - 1)) {
		udp_log_hexdump(NULL, s, ret);
	} else {
		ret = log_server_sendto((void *)s, ret + 1);
	}

	return (ret - 1);
}

static uint8_t calc_crc8(void *data, size_t size)
{
	uint8_t crc = 0;
	uint8_t *p = (uint8_t *)data;

	while(size > 0) {
		crc += *p;

		p++;
		size--;
	}

	return crc;
}

static int chunk_sendto(uint32_t fn, int server_sock, void *data, size_t size, struct sockaddr *addr, socklen_t addr_size)
{
	int ret = 0;
	char *p = (char *)data;
	size_t sent = 0;
	size_t head_size = sizeof(request_t);
	size_t payload = SEND_BUFFER_SIZE - head_size;

	if(data == NULL) {
		ret = -1;
		return ret;
	}

	while(sent < size) {
		size_t left = size - sent;
		size_t len = (left > payload) ? payload : left;
		request_t *request = (request_t *)probe_send_buffer;
		size_t send_size = len + head_size;
		int count;

		request->header.magic = 0xa5a55a5a;
		request->header.total_size = size;
		request->header.data_offset = sent;
		request->header.data_size = len;

		request->payload.fn = fn;
		request->payload.stage = 0;
		memcpy(request + 1, p + sent, len);

		request->header.crc = calc_crc8(((header_info_t *)&request->header) + 1, len + sizeof(payload_info_t));

		count = sendto(server_sock, request, send_size, 0,  addr, addr_size);

		if(count == send_size) {
			sent += len;
		} else {
			ret = -1;
			break;
		}
	}

	if(ret == 0) {
		ret = sent;
	}

	return ret;
}

static int probe_server_chunk_sendto(uint32_t fn, void *data, size_t size)
{
	return chunk_sendto(fn, probe_server_sock, data, size, (struct sockaddr *)&probe_client_address_in, sizeof(probe_client_address_in));
}

static void loopback(request_t *request)
{
	int send_size = request->header.data_size + sizeof(request_t);

	log_client_address_in = probe_client_address_in;
	log_client_address_in.sin_port = htons(LOG_TOOL_PORT);
	log_client_address_valid = 1;

	sendto(probe_server_sock, request, send_size, 0, (struct sockaddr *)&probe_client_address_in, sizeof(probe_client_address_in));
}

static void fn1(request_t *request)
{
	probe_server_chunk_sendto(request->payload.fn, "hello!", strlen("hello!"));
}

static void fn2(request_t *request)
{
	probe_server_chunk_sendto(request->payload.fn, (void *)0x8000000, 256);
}

static void fn3(request_t *request)
{
	static uint32_t file_crc32 = 0;

	int send_size = request->header.data_size + sizeof(request_t);
	uint32_t data_size = request->header.data_size;
	uint32_t stage = request->payload.stage;
	uint8_t *data = (uint8_t *)(request + 1);

	if(stage == 0) {
	} else if(stage == 1) {
		if(data_size == 4) {
			uint32_t *p = (uint32_t *)data;
			file_crc32 = *p;
		}
	} else if(stage == 2) {
		file_crc32 = file_crc32;
	}

	sendto(probe_server_sock, request, send_size, 0, (struct sockaddr *)&probe_client_address_in, sizeof(probe_client_address_in));
}

static void p_select_timeout_statistic(void)
{
	uint32_t duration = osKernelSysTick() - select_timeout_count_stamp;

	if(duration != 0) {
		udp_log_printf("select_timeout_count per second:%d\n", (select_timeout_count * 1000) / duration);
	}

	select_timeout_count = 0;
}

static int p_host(struct hostent *ent)
{
	int ret = 0;
	char **cp;

	if(ent == NULL) {
		ret = -1;
		return ret;
	}

	udp_log_printf("\n");

	udp_log_printf("h_name:%s\n", ent->h_name);
	udp_log_printf("h_aliases:\n");
	cp = ent->h_aliases;

	while(*cp != NULL) {
		udp_log_printf("%s\n", *cp);
		cp += 1;

		if(*cp != NULL) {
			//udp_log_printf(", ");
		}
	}

	udp_log_printf("h_addrtype:%d\n", ent->h_addrtype);

	udp_log_printf("h_length:%d\n", ent->h_length);

	udp_log_printf("h_addr_list:\n");
	cp = ent->h_addr_list;

	while(*cp != NULL) {
		udp_log_printf("%s\n", inet_ntoa(**cp));
		cp += 1;

		if(*cp != NULL) {
			//udp_log_printf(", ");
		}
	}

	return ret;
}

static void get_host_by_name(char *content, uint32_t size)
{
	struct hostent *ent;
	char hostname[RECV_BUFFER_SIZE];
	int ret;
	int fn;
	int catched;

	//udp_log_hexdump("content", (const char *)content, size);

	ret = sscanf(content, "%d %s%n", &fn, hostname, &catched);

	if(ret == 2) {
		udp_log_printf("hostname:%s!\n", hostname);
		ent = gethostbyname(hostname);
		p_host(ent);
	} else {
		udp_log_printf("no hostname!\n");
	}
}

static void fn4(request_t *request)
{
	log_client_address_in = probe_client_address_in;
	log_client_address_in.sin_port = htons(LOG_TOOL_PORT);
	log_client_address_valid = 1;

	p_select_timeout_statistic();

	udp_log_printf("local host ip:%s\n", inet_ntoa(gnetif.ip_addr));

	get_host_by_name((char *)(request + 1), request->header.data_size);
	memset(request, 0, RECV_BUFFER_SIZE);
}

static void fn5(request_t *request)
{
	int size = xPortGetFreeHeapSize();
	uint8_t *os_thread_info;

	log_client_address_in = probe_client_address_in;
	log_client_address_in.sin_port = htons(LOG_TOOL_PORT);
	log_client_address_valid = 1;

	udp_log_printf("free heap size:%d\n", size);

	if(size < 4 * 1024) {
		return;
	}

	size = 1024;

	os_thread_info = (uint8_t *)os_alloc(size);

	if(os_thread_info == NULL) {
		return;
	}

	osThreadList(os_thread_info);

	udp_log_puts((const char *)os_thread_info);

	os_free(os_thread_info);
}

serve_map_t serve_map[] = {
	{0, loopback},
	{1, fn1},
	{2, fn2},
	{3, fn3},
	{4, fn4},
	{5, fn5},
};


static int init_broadcast_socket(void)
{
	int ret = 0;
	const int opt = -1;
	int nb = 0;

	broadcast_sock = socket(AF_INET, SOCK_DGRAM, 0);

	if(broadcast_sock  == -1) {
		ret = -1;
		return ret;
	}

	nb = setsockopt(broadcast_sock, SOL_SOCKET, SO_BROADCAST, (char *)&opt, sizeof(opt)); //设置套接字类型

	if(nb == -1) {
		ret = -1;
		return ret;
	}

	memset(&broadcast_addr, 0x00, sizeof(struct sockaddr_in));
	broadcast_addr.sin_family = AF_INET;
	broadcast_addr.sin_addr.s_addr = htonl(INADDR_BROADCAST); //套接字地址为广播地址
	broadcast_addr.sin_port = htons(BROADCAST_PORT); //套接字广播端口号为BROADCAST_PORT

	return ret;
}

static int init_server_socket(uint16_t port)
{
	int ret = 0;
	int server_sock = -1;
	struct sockaddr_in server_address_in;

	server_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	if(server_sock == -1) {
		return server_sock;
	}

	memset(&server_address_in, 0x00, sizeof(server_address_in));
	server_address_in.sin_family = AF_INET;
	server_address_in.sin_addr.s_addr = htonl(IPADDR_ANY); //套接字地址为任意地址
	server_address_in.sin_port = htons(port);

	ret = bind(server_sock, (struct sockaddr *)&server_address_in, sizeof(server_address_in));

	if(ret < 0) {
		close(server_sock);
		server_sock = -1;
	}

	return server_sock;
}

static int init_probe_server_socket(void)
{
	int ret = 0;
	probe_server_sock = init_server_socket(PROBE_TOOL_PORT);

	if(probe_server_sock < 0) {
		ret = -1;
		return ret;
	}

	return ret;
}


static void request_parse(uint8_t *buffer, uint16_t size, uint8_t **prequest, uint16_t *request_size)
{
	uint8_t *start = buffer;
	size_t valid_size = size;

	*prequest = start;
	*request_size = 0;

	while(valid_size >= sizeof(request_t)) {
		request_t *request = (request_t *)start;
		size_t payload = valid_size - sizeof(request_t);
		size_t max_payload = RECV_BUFFER_SIZE - sizeof(request_t);

		*prequest = start;
		*request_size = 0;

		if(request->header.magic != 0xa5a55a5a) {//无效
			start++;
			valid_size--;
			continue;
		}

		if(request->header.data_size > max_payload) {//无效
			start++;
			valid_size--;
			continue;
		}

		if(request->header.data_size > payload) {//还要收
			break;
		}

		if(request->header.crc != calc_crc8(((header_info_t *)&request->header) + 1, request->header.data_size + sizeof(payload_info_t))) {//无效
			start++;
			valid_size--;
			continue;
		}

		*request_size = request->header.data_size + sizeof(request_t);
		break;
	}
}

static void probe_server_process_message(int size)
{
	int map_size = sizeof(serve_map) / sizeof(serve_map_t);
	int i;
	uint8_t found = 0;
	request_t *request;
	uint8_t *request_buffer;
	uint16_t request_size = 0;

	request_parse((uint8_t *)probe_recv_buffer, size, (uint8_t **)&request_buffer, &request_size);

	if(request_size == 0) {
		return;
	}

	request = (request_t *)request_buffer;

	for(i = 1; i < map_size; i++) {
		serve_map_t *serve_item = serve_map + i;

		if(serve_item->fn == request->payload.fn) {
			log_client_address_valid = 0;
			serve_item->response(request);
			found = 1;
			break;
		}
	}

	if(found == 0) {
		serve_map_t *serve_item = serve_map + 0;
		serve_item->response(request);
	}
}

static void probe_server_recv(void)
{
	int ret = 0;
	struct fd_set fds;
	struct timeval tv = {1, 0};
	int max_fd = 0;

	FD_ZERO(&fds);
	FD_SET(probe_server_sock, &fds);

	if(probe_server_sock > max_fd) {
		max_fd = probe_server_sock;
	}

	ret = select(max_fd + 1, &fds, NULL, NULL, &tv);

	if(select_timeout_count == 0) {
		select_timeout_count_stamp = osKernelSysTick();
	}

	select_timeout_count++;

	if(ret > 0) {
		if(FD_ISSET(probe_server_sock, &fds)) {
			socklen_t socklen = sizeof(probe_client_address_in);
			ret = recvfrom(probe_server_sock, probe_recv_buffer, RECV_BUFFER_SIZE, 0, (struct sockaddr *)&probe_client_address_in, &socklen);

			if(ret > 0) {
				probe_server_process_message(ret);
			}

		}
	}
}

static void send_broadcast_info(void)
{
	static uint32_t stamp = 0;
	uint32_t ticks;

	ticks = osKernelSysTick();

	if(ticks - stamp >= 1000) {
		char *msg = "hello!";
		size_t len =  strlen(msg);
		int ret = sendto(broadcast_sock, msg, len, 0, (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr)); //向广播地址发布消息

		if(ret < 0) {
		}

		stamp = ticks;
	}
}

static int init_probe_sockets(void)
{
	int ret = 0;

	ret = init_broadcast_socket();

	if(ret != 0) {
		return ret;
	}

	ret = init_probe_server_socket();

	if(ret != 0) {
		return ret;
	}

	return ret;
}


static void uninit_probe_sockets(void)
{
	close(broadcast_sock);
	broadcast_sock = -1;
	close(probe_server_sock);
	probe_server_sock = -1;
}

void task_probe_tool(void const *argument)
{
	uint8_t inited = 0;
	uint8_t retry = 0;

	dhcp_release(&gnetif);
	dhcp_start(&gnetif);
	osDelay(100);

	while(1) {
		if(inited == 0) {
			if(init_probe_sockets() == 0) {
				inited = 1;
			} else {
				uninit_probe_sockets();
				retry++;

				if(retry >= 10) {
					dhcp_release(&gnetif);
					dhcp_start(&gnetif);
				}

				osDelay(1000);
				continue;
			}
		}

		probe_server_recv();
		send_broadcast_info();
		//osDelay(1000);
	}
}
