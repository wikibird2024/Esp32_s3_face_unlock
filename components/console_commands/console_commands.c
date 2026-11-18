#include "console_commands.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "console_json.h"
#include "door_control.h"
#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_console.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "net_comm.h"
#include "system_logic.h"

static const char *TAG = "console";

/* Letters, digits, '_' and '-': keeps names safe in JSON and log lines. */
#define ENROLL_NAME_ALLOWED_CHARS                                              \
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-"
#define ELF_SHA256_SHORT_LEN 8
/* vTaskList() needs about 40 bytes per task plus the 0 byte. */
#define TASK_LIST_BYTES_PER_TASK 48

static face_db_t *s_face_db;
/* Only the console task writes here; it is static to spare its stack. */
static char s_json_buffer[CONSOLE_JSON_STATUS_MAX_LEN];

static bool is_json_argument(int argc, char **argv) {
  return argc == 2 && strcmp(argv[1], "json") == 0;
}

static bool face_db_ready(void) {
  if (s_face_db == NULL) {
    printf("Face store not available (see boot log).\n");
    return false;
  }
  return true;
}

static int report(esp_err_t err) {
  if (err != ESP_OK) {
    printf("Failed: %s\n", esp_err_to_name(err));
    return 1;
  }
  return 0;
}

static int print_status_json(void) {
  system_logic_status_t status = {0};
  const bool has_status = system_logic_get_status(&status) == ESP_OK;
  char ip[NET_COMM_IP_MAX_LEN];
  net_comm_get_ip(ip);
  char elf_sha256_short[ELF_SHA256_SHORT_LEN + 1];
  esp_app_get_elf_sha256(elf_sha256_short, sizeof(elf_sha256_short));
  const esp_app_desc_t *app = esp_app_get_description();

  const console_status_json_t json = {
      .state_name = has_status ? unlock_state_name(status.state) : "",
      .state_id = (int)status.state,
      .frame_count = status.frame_count,
      .face_frame_count = status.face_frame_count,
      .inference_ms = status.last_inference_ms,
      .last_face_id = status.last_face_id,
      .last_similarity = status.last_similarity,
      .min_similarity = status.min_similarity,
      .waiting_request_id = status.waiting_request_id,
      .is_door_open = door_control_is_open(),
      .is_wifi_enabled = CONFIG_NET_COMM_WIFI_SSID[0] != '\0',
      .is_wifi_connected = net_comm_is_connected(),
      .ip = ip,
      .has_face_store = s_face_db != NULL,
      .face_count = s_face_db ? (uint32_t)face_db_count(s_face_db) : 0,
      .face_capacity = s_face_db ? (uint32_t)face_db_capacity(s_face_db) : 0,
      .face_skipped =
          s_face_db ? (uint32_t)face_db_skipped_count(s_face_db) : 0,
      .heap_internal = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
      .heap_psram = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
      .heap_internal_min =
          (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
      .app_version = app->version,
      .idf_version = app->idf_ver,
      .compile_date = app->date,
      .compile_time = app->time,
      .elf_sha256_short = elf_sha256_short,
      .uptime_ms = (uint64_t)esp_timer_get_time() / 1000,
  };
  if (console_json_write_status(&json, s_json_buffer, sizeof(s_json_buffer)) <
      0) {
    printf("Failed: status line too long\n");
    return 1;
  }
  printf("@status %s\n", s_json_buffer);
  return 0;
}

static int command_status(int argc, char **argv) {
  if (is_json_argument(argc, argv)) {
    return print_status_json();
  }
  if (argc != 1) {
    printf("Usage: status [json]\n");
    return 1;
  }
  system_logic_status_t status;
  if (system_logic_get_status(&status) == ESP_OK) {
    printf("state            %s\n", unlock_state_name(status.state));
    printf("frames           %lu (with face %lu)\n",
           (unsigned long)status.frame_count,
           (unsigned long)status.face_frame_count);
    printf("inference        %lu ms\n",
           (unsigned long)status.last_inference_ms);
    printf("last best match  id %u, similarity %.3f (need %.2f)\n",
           status.last_face_id, status.last_similarity, status.min_similarity);
    if (status.waiting_request_id != 0) {
      printf("approval         waiting #%lu (approve / deny %lu)\n",
             (unsigned long)status.waiting_request_id,
             (unsigned long)status.waiting_request_id);
    }
  }
  printf("door             %s\n", door_control_is_open() ? "open" : "locked");
  if (net_comm_is_connected()) {
    char ip[NET_COMM_IP_MAX_LEN];
    net_comm_get_ip(ip);
    printf("wifi             connected, live view http://%s/\n", ip);
  } else {
    printf("wifi             %s\n", CONFIG_NET_COMM_WIFI_SSID[0] == '\0'
                                        ? "off (no SSID in menuconfig)"
                                        : "not connected");
  }
  if (s_face_db != NULL) {
    printf("faces            %u / %u (skipped at boot: %u)\n",
           (unsigned)face_db_count(s_face_db),
           (unsigned)face_db_capacity(s_face_db),
           (unsigned)face_db_skipped_count(s_face_db));
  }
  printf("free heap        internal %u, psram %u\n",
         (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
         (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  return 0;
}

static int print_list_json(void) {
  const size_t count = face_db_count(s_face_db);
  face_db_entry_t *entries = calloc(count > 0 ? count : 1, sizeof(*entries));
  const size_t capacity = count * CONSOLE_JSON_LIST_ENTRY_MAX_LEN + 8;
  char *line = malloc(capacity);
  int result = 1;
  if (entries == NULL || line == NULL) {
    printf("Failed: %s\n", esp_err_to_name(ESP_ERR_NO_MEM));
  } else {
    size_t found = 0;
    for (size_t i = 0; i < count; i++) {
      if (face_db_get_entry(s_face_db, i, &entries[found]) == FACE_DB_OK) {
        found++;
      }
    }
    if (console_json_write_list(entries, found, line, capacity) < 0) {
      printf("Failed: list line too long\n");
    } else {
      printf("@list %s\n", line);
      result = 0;
    }
  }
  free(entries);
  free(line);
  return result;
}

static int command_list(int argc, char **argv) {
  if (!face_db_ready()) {
    return 1;
  }
  if (is_json_argument(argc, argv)) {
    return print_list_json();
  }
  if (argc != 1) {
    printf("Usage: list [json]\n");
    return 1;
  }
  const size_t count = face_db_count(s_face_db);
  if (count == 0) {
    printf("No faces enrolled. Use: enroll <name>\n");
    return 0;
  }
  printf("  id  name\n");
  for (size_t i = 0; i < count; i++) {
    face_db_entry_t entry;
    if (face_db_get_entry(s_face_db, i, &entry) == FACE_DB_OK) {
      printf("%4u  %s\n", entry.id, entry.name);
    }
  }
  return 0;
}

static int command_enroll(int argc, char **argv) {
  if (argc != 2) {
    printf("Usage: enroll <name>\n");
    return 1;
  }
  if (!face_db_ready()) {
    return 1;
  }
  if (strlen(argv[1]) > FACE_DB_NAME_MAX_LEN) {
    printf("Name too long (max %u characters)\n", FACE_DB_NAME_MAX_LEN);
    return 1;
  }
  if (strspn(argv[1], ENROLL_NAME_ALLOWED_CHARS) != strlen(argv[1])) {
    printf("Name: letters, digits, _ and - only\n");
    return 1;
  }
  int result = report(system_logic_request_enroll(argv[1]));
  if (result == 0) {
    printf("Look at the camera. Watch the log for the result.\n");
  }
  return result;
}

static int command_cancel(int argc, char **argv) {
  (void)argc;
  (void)argv;
  return report(system_logic_request_cancel_enroll());
}

static int command_delete(int argc, char **argv) {
  if (argc != 2) {
    printf("Usage: delete <id>   (ids from 'list')\n");
    return 1;
  }
  if (!face_db_ready()) {
    return 1;
  }
  char *end = NULL;
  const unsigned long id = strtoul(argv[1], &end, 10);
  if (end == argv[1] || *end != '\0' || id == 0 || id > UINT16_MAX) {
    printf("Not a valid id: %s\n", argv[1]);
    return 1;
  }
  face_db_status_t status = face_db_delete(s_face_db, (uint16_t)id);
  printf("%s\n",
         status == FACE_DB_OK ? "Deleted" : face_db_status_name(status));
  return status == FACE_DB_OK ? 0 : 1;
}

static int command_clear(int argc, char **argv) {
  if (argc != 2 || strcmp(argv[1], "yes") != 0) {
    printf("This deletes every enrolled face. Type: clear yes\n");
    return 1;
  }
  if (!face_db_ready()) {
    return 1;
  }
  face_db_status_t status = face_db_clear(s_face_db);
  printf("%s\n", status == FACE_DB_OK ? "All faces deleted"
                                      : face_db_status_name(status));
  return status == FACE_DB_OK ? 0 : 1;
}

static int command_unlock(int argc, char **argv) {
  (void)argc;
  (void)argv;
  return report(system_logic_request_unlock());
}

/* approve <id> / deny <id>: the console stands in for the phone. */
static int command_answer(int argc, char **argv, bool is_approve) {
  if (argc != 2) {
    printf("Usage: %s <id>   (id from the log or 'status')\n", argv[0]);
    return 1;
  }
  char *end = NULL;
  errno = 0;
  const unsigned long request_id = strtoul(argv[1], &end, 10);
  if (end == argv[1] || *end != '\0' || errno == ERANGE || request_id == 0) {
    printf("Not a valid id: %s\n", argv[1]);
    return 1;
  }
  return report(is_approve ? system_logic_request_approve((uint32_t)request_id)
                           : system_logic_request_deny((uint32_t)request_id));
}

static int command_approve(int argc, char **argv) {
  return command_answer(argc, argv, true);
}

static int command_deny(int argc, char **argv) {
  return command_answer(argc, argv, false);
}

static int command_stats(int argc, char **argv) {
  if (argc != 2) {
    printf("Usage: stats <period_ms>   (0 = off)\n");
    return 1;
  }
  char *end = NULL;
  errno = 0;
  const unsigned long period_ms = strtoul(argv[1], &end, 10);
  if (end == argv[1] || *end != '\0' || errno == ERANGE ||
      period_ms > UINT32_MAX) {
    printf("Not a valid period: %s\n", argv[1]);
    return 1;
  }
  const uint32_t applied_ms =
      system_logic_set_stats_period_ms((uint32_t)period_ms);
  if (applied_ms == 0) {
    printf("Telemetry off\n");
  } else {
    printf("Telemetry every %lu ms\n", (unsigned long)applied_ms);
  }
  return 0;
}

static void print_heap_line(const char *label, uint32_t caps) {
  printf("%s free %u, min %u, largest %u\n", label,
         (unsigned)heap_caps_get_free_size(caps),
         (unsigned)heap_caps_get_minimum_free_size(caps),
         (unsigned)heap_caps_get_largest_free_block(caps));
}

static int command_free(int argc, char **argv) {
  (void)argc;
  (void)argv;
  print_heap_line("internal", MALLOC_CAP_INTERNAL);
  print_heap_line("psram", MALLOC_CAP_SPIRAM);
  return 0;
}

static int command_restart(int argc, char **argv) {
  (void)argc;
  (void)argv;
  printf("Restarting\n");
  fflush(stdout);
  vTaskDelay(pdMS_TO_TICKS(100));
  esp_restart();
}

static int command_log(int argc, char **argv) {
  static const struct {
    const char *name;
    esp_log_level_t level;
  } levels[] = {
      {"none", ESP_LOG_NONE},   {"error", ESP_LOG_ERROR},
      {"warn", ESP_LOG_WARN},   {"info", ESP_LOG_INFO},
      {"debug", ESP_LOG_DEBUG}, {"verbose", ESP_LOG_VERBOSE},
  };
  if (argc != 3) {
    printf("Usage: log <tag|*> <none|error|warn|info|debug|verbose>\n");
    return 1;
  }
  for (size_t i = 0; i < sizeof(levels) / sizeof(levels[0]); i++) {
    if (strcmp(argv[2], levels[i].name) == 0) {
      esp_log_level_set(argv[1], levels[i].level);
      printf("Log level of '%s' set to %s\n", argv[1], levels[i].name);
      return 0;
    }
  }
  printf("Not a valid level: %s\n", argv[2]);
  return 1;
}

#if CONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS
static int command_tasks(int argc, char **argv) {
  (void)argc;
  (void)argv;
  const size_t capacity = uxTaskGetNumberOfTasks() * TASK_LIST_BYTES_PER_TASK;
  char *table = malloc(capacity);
  if (table == NULL) {
    printf("Failed: %s\n", esp_err_to_name(ESP_ERR_NO_MEM));
    return 1;
  }
  vTaskList(table);
  printf("name            state prio stack num core\n%s", table);
  free(table);
  return 0;
}
#endif

static esp_err_t register_commands(void) {
  const esp_console_cmd_t commands[] = {
      {.command = "status",
       .help = "Show state, last match, faces and memory: status [json]",
       .hint = "[json]",
       .func = command_status},
      {.command = "list",
       .help = "List enrolled faces: list [json]",
       .hint = "[json]",
       .func = command_list},
      {.command = "enroll",
       .help = "Enroll the next face seen: enroll <name>",
       .hint = "<name>",
       .func = command_enroll},
      {.command = "cancel",
       .help = "Cancel a running enroll",
       .func = command_cancel},
      {.command = "delete",
       .help = "Delete one face: delete <id>",
       .hint = "<id>",
       .func = command_delete},
      {.command = "clear",
       .help = "Delete all faces: clear yes",
       .hint = "yes",
       .func = command_clear},
      {.command = "unlock",
       .help = "Open the door now (manual override)",
       .func = command_unlock},
      {.command = "approve",
       .help = "Approve a face waiting for approval: approve <id>",
       .hint = "<id>",
       .func = command_approve},
      {.command = "deny",
       .help = "Deny a face waiting for approval: deny <id>",
       .hint = "<id>",
       .func = command_deny},
      {.command = "stats",
       .help = "Print telemetry lines every <period_ms> ms (0 = off)",
       .hint = "<period_ms>",
       .func = command_stats},
      {.command = "free", .help = "Show free heap", .func = command_free},
      {.command = "restart", .help = "Reboot", .func = command_restart},
      {.command = "log",
       .help = "Set a log level: log <tag|*> <level>",
       .hint = "<tag> <level>",
       .func = command_log},
#if CONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS
      {.command = "tasks",
       .help = "List FreeRTOS tasks",
       .func = command_tasks},
#endif
  };
  for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
    ESP_RETURN_ON_ERROR(esp_console_cmd_register(&commands[i]), TAG,
                        "register '%s' failed", commands[i].command);
  }
  return ESP_OK;
}

esp_err_t console_commands_start(face_db_t *face_db) {
  s_face_db = face_db;

  esp_console_repl_t *repl = NULL;
  esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
  repl_config.prompt = "face>";
  repl_config.max_cmdline_length = 128;

#if CONFIG_ESP_CONSOLE_UART_DEFAULT || CONFIG_ESP_CONSOLE_UART_CUSTOM
  esp_console_dev_uart_config_t device_config =
      ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
  ESP_RETURN_ON_ERROR(
      esp_console_new_repl_uart(&device_config, &repl_config, &repl), TAG,
      "uart repl failed");
#elif CONFIG_ESP_CONSOLE_USB_CDC
  esp_console_dev_usb_cdc_config_t device_config =
      ESP_CONSOLE_DEV_CDC_CONFIG_DEFAULT();
  ESP_RETURN_ON_ERROR(
      esp_console_new_repl_usb_cdc(&device_config, &repl_config, &repl), TAG,
      "usb cdc repl failed");
#elif CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
  esp_console_dev_usb_serial_jtag_config_t device_config =
      ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
  ESP_RETURN_ON_ERROR(
      esp_console_new_repl_usb_serial_jtag(&device_config, &repl_config, &repl),
      TAG, "usb serial jtag repl failed");
#else
#error                                                                         \
    "No console output selected in menuconfig (Component config > ESP System Settings > Channel for console output)"
#endif

  /* esp_console_new_repl_*() already registered "help". */
  ESP_RETURN_ON_ERROR(register_commands(), TAG, "commands failed");
  ESP_RETURN_ON_ERROR(esp_console_start_repl(repl), TAG, "repl start failed");
  return ESP_OK;
}
