#ifndef _TASK_PROBE_TOOL_H
#define _TASK_PROBE_TOOL_H
#ifdef __cplusplus
extern "C"
{
#endif

#define BROADCAST_PORT 6000
#define PROBE_TOOL_PORT 6001
#define LOG_TOOL_PORT 6002
#define RECV_BUFFER_SIZE 128
#define SEND_BUFFER_SIZE 128
#define LOG_BUFFER_SIZE 128

typedef struct {
	uint32_t magic;
	uint32_t total_size;
	uint32_t data_offset;
	uint32_t data_size;
	uint8_t crc;
} header_info_t;

typedef struct {
	uint32_t fn;
	uint32_t stage;
} payload_info_t;

typedef struct {
	header_info_t header;
	payload_info_t payload;
} request_t;

typedef void (*response_t)(request_t *request);

typedef struct {
	uint32_t fn;
	response_t response;
} serve_map_t;

int udp_log_printf(const char *fmt, ...);
void udp_log_hexdump(const char *label, const char *data, int len);

void task_probe_tool(void const *argument);
#ifdef __cplusplus
}
#endif
#endif //_TASK_PROBE_TOOL_H
