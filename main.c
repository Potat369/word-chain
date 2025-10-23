#include <concord/discord.h>
#include <concord/log.h>
#include <sqlite3.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <signal.h>

#define GUILD_ID 1380548465909698651

sqlite3 *DB;

void on_ready(struct discord *client, const struct discord_ready *event) {
  // For seamless restart
  char* KILL_ON_START = getenv("KILL_ON_START");
  if (KILL_ON_START != NULL) {
    kill(atoi(KILL_ON_START), SIGTERM);
  }

  struct discord_create_global_application_command set_channel = {
    .name = "set_channel",
    .description = "Sets channel for the game. ⚠️WARNING ⚠️ This will erase channel's topic.",
    .options = &(struct discord_application_command_options) {
      .size = 1,
      .array = (struct discord_application_command_option[]) {
        {
          .type = DISCORD_APPLICATION_OPTION_CHANNEL,
          .name = "channel",
          .description = "Channel",
          .required = true,
          .channel_types = &(struct integers) {
            .size = 1,
            .array = (int[]) { DISCORD_CHANNEL_GUILD_TEXT },
          },
        },
      },
    },
  };

  struct discord_create_global_application_command start_params = {
    .name = "start",
    .description = "Starts the game in previously specified channel",
  };

  struct discord_create_global_application_command stop_params = {
    .name = "stop",
    .description = "Stops the game",
  };

  discord_create_global_application_command(client, event->application->id, &set_channel, NULL);
  discord_create_global_application_command(client, event->application->id, &start_params, NULL);
  discord_create_global_application_command(client, event->application->id, &stop_params, NULL);
}

void on_interaction_create(struct discord *client, const struct discord_interaction *event) {
  if (event->type != DISCORD_INTERACTION_APPLICATION_COMMAND)
    return;

  if (strcmp(event->data->name, "set_channel") == 0) {
    if (!event->data || !event->data->options) 
      return;

    sqlite3_stmt *stmt;
    sqlite3_prepare(DB, "INSERT INTO guilds (id, channel, typed_words) VALUES (?1, ?2, '[]') ON CONFLICT(id) DO UPDATE SET channel=?2;", -1, &stmt, NULL);
    sqlite3_bind_int64(stmt, 1, event->guild_id);
    sqlite3_bind_int64(stmt, 2, strtoull(event->data->options->array[0].value, NULL, 10));
    int status = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (status != SQLITE_DONE) {
      log_error("%d", status);
    }

    struct discord_interaction_response params = {
      .type = DISCORD_INTERACTION_CHANNEL_MESSAGE_WITH_SOURCE,
      .data = &(struct discord_interaction_callback_data){ 
        .content = status != SQLITE_DONE ? "An error occured" : "Success", 
        .flags = DISCORD_MESSAGE_EPHEMERAL 
      }
    };
    discord_create_interaction_response(client, event->id, event->token, &params, NULL);

  } else if (strcmp(event->data->name, "start") == 0) {

    sqlite3_stmt *stmt;
    sqlite3_prepare(DB, "SELECT channel, started FROM guilds WHERE id=?1;", -1, &stmt, NULL);
    sqlite3_bind_int64(stmt, 1, event->guild_id);
    int status = sqlite3_step(stmt);

    if (status != SQLITE_ROW) {
      struct discord_interaction_response params = {
        .type = DISCORD_INTERACTION_CHANNEL_MESSAGE_WITH_SOURCE,
        .data = &(struct discord_interaction_callback_data){ 
          .content = "You must specify channel with /set_channel first!", 
          .flags = DISCORD_MESSAGE_EPHEMERAL 
        }
      };
      discord_create_interaction_response(client, event->id, event->token, &params, NULL);
    } else if (sqlite3_column_int(stmt, 1) == true) {
      struct discord_interaction_response params = {
        .type = DISCORD_INTERACTION_CHANNEL_MESSAGE_WITH_SOURCE,
        .data = &(struct discord_interaction_callback_data){ 
          .content = "Game is already started. Use /stop to stop it", 
          .flags = DISCORD_MESSAGE_EPHEMERAL 
        }
      };
      discord_create_interaction_response(client, event->id, event->token, &params, NULL);
    } else {
      struct discord_interaction_response response_params = {
        .type = DISCORD_INTERACTION_CHANNEL_MESSAGE_WITH_SOURCE,
        .data = &(struct discord_interaction_callback_data){ 
          .content = "Succesfully started the game",
          .flags = DISCORD_MESSAGE_EPHEMERAL 
        }
      };
      discord_create_interaction_response(client, event->id, event->token, &response_params, NULL);

      uint64_t channel = sqlite3_column_int64(stmt, 0);
      char start_character = rand() % 25 + 97;

      sqlite3_finalize(stmt);
      sqlite3_prepare(DB, "UPDATE guilds SET started=TRUE, last_char=?1 WHERE id=?2", -1, &stmt, NULL);
      sqlite3_bind_int(stmt, 1, start_character);
      sqlite3_bind_int64(stmt, 2, event->guild_id);
      sqlite3_step(stmt);

      char msg[] = "Word Chain game has begun. Starting with: `%`";
      msg[sizeof(msg) / sizeof(char) - 3] = start_character;

      struct discord_create_message create_params = {
        .content = msg
      };

      discord_create_message(client, channel, &create_params, NULL);
    }
    sqlite3_finalize(stmt);
  } else if (strcmp(event->data->name, "stop") == 0) {
    sqlite3_stmt* stmt;
    sqlite3_prepare(DB, "UPDATE guilds SET started=FALSE, last_user=NULL, typed_words='[]' WHERE id=?1;", -1, &stmt, NULL);
    sqlite3_bind_int64(stmt, 1, event->guild_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    struct discord_interaction_response response_params = {
      .type = DISCORD_INTERACTION_CHANNEL_MESSAGE_WITH_SOURCE,
      .data = &(struct discord_interaction_callback_data){ 
        .content = "Succesfully stopped the game",
        .flags = DISCORD_MESSAGE_EPHEMERAL 
      }
    };
    discord_create_interaction_response(client, event->id, event->token, &response_params, NULL);
  }
}

bool isValidWord(char* str) {
  char* c = str;
  while (*c) {
    if (!isalpha(*c)) {
      return false;
    }
    *c++;
  }
  // struct dict* d;
  // HASH_FIND_STR(dictionary, str, d);
  // return d != NULL;
  return true;
}

void on_message_create(struct discord* client, const struct discord_message *event) {
  if (event->author->bot == true || event->author->System == true) return;

  sqlite3_stmt *stmt;
  sqlite3_prepare(DB, "SELECT channel, last_user, last_char, started FROM guilds WHERE id=?1;", -1, &stmt, NULL);
  sqlite3_bind_int64(stmt, 1, event->guild_id);
  int status = sqlite3_step(stmt);

  if (status != SQLITE_ROW) return;

  bool started = sqlite3_column_int(stmt, 3);
  int64_t channel_id = sqlite3_column_int64(stmt, 0);
  for(int i = 0; event->content[i]; i++){
    event->content[i] = tolower(event->content[i]);
  }
  if (started == true && channel_id == event->channel_id) {
    int64_t last_user = sqlite3_column_int64(stmt, 1);
    char last_char = sqlite3_column_int(stmt, 2);
    struct discord_message_reference reference = {
      .message_id = event->id,
      .channel_id = event->channel_id,
      .guild_id = event->guild_id,
      .fail_if_not_exists = true
    };
    if (last_user == event->author->id) {
      struct discord_create_message create_params = {
        .content = "Not your turn",
        .message_reference = &reference
      };
      discord_create_reaction(client, event->channel_id, event->id, 0, "❌", NULL);
      discord_create_message(client, event->channel_id, &create_params, NULL);
    } else if (isValidWord(event->content)) {
      if (event->content[0] == last_char) {
        sqlite3_finalize(stmt);
        sqlite3_prepare(DB, "SELECT json_each.value FROM guilds, json_each(guilds.typed_words) WHERE guilds.id=?1 AND json_each.value=?2;", -1, &stmt, NULL);
        sqlite3_bind_int64(stmt, 1, event->guild_id);
        sqlite3_bind_text(stmt, 2, event->content, -1, SQLITE_STATIC);
        status = sqlite3_step(stmt);
        if (status == SQLITE_DONE) {
          sqlite3_finalize(stmt);
          sqlite3_prepare(DB, "SELECT word FROM words WHERE word=?1;", -1, &stmt, NULL);
          sqlite3_bind_text(stmt, 1, event->content, -1, SQLITE_STATIC);
          status = sqlite3_step(stmt);
          if (status == SQLITE_ROW) {
            sqlite3_finalize(stmt);
            sqlite3_prepare(DB, "UPDATE guilds SET last_user=?1, last_char=?2, typed_words=json_insert(guilds.typed_words, '$[#]', ?3) WHERE id=?4", -1, &stmt, NULL);
            sqlite3_bind_int64(stmt, 1, event->author->id);
            sqlite3_bind_int(stmt, 2, event->content[strlen(event->content) - 1]);
            sqlite3_bind_text(stmt, 3, event->content, -1, SQLITE_STATIC);
            sqlite3_bind_int64(stmt, 4, event->guild_id);
            sqlite3_step(stmt);
            discord_create_reaction(client, event->channel_id, event->id, 0, "✅", NULL);
          } else if (status == SQLITE_DONE) {
            struct discord_create_message create_params = {
              .content = "Not found in dictionary",
              .message_reference = &reference
            };
            discord_create_reaction(client, event->channel_id, event->id, 0, "❌", NULL);
            discord_create_message(client, event->channel_id, &create_params, NULL);
          }
        } else {
          struct discord_create_message create_params = {
            .content = "Was already entered",
            .message_reference = &reference
          };
          discord_create_reaction(client, event->channel_id, event->id, 0, "❌", NULL);
          discord_create_message(client, event->channel_id, &create_params, NULL);
        }
      } else {
        char content[] = "Must begin with: `%`";
        content[sizeof(content) / sizeof(char) - 3] = last_char;
        struct discord_create_message create_params = {
          .content = content,
          .message_reference = &reference
        };
        discord_create_reaction(client, event->channel_id, event->id, 0, "❌", NULL);
        discord_create_message(client, event->channel_id, &create_params, NULL);
      }
    } else {
      struct discord_create_message create_params = {
        .content = "Invalid word",
        .message_reference = &reference
      };
      discord_create_reaction(client, event->channel_id, event->id, 0, "❌", NULL);
      discord_create_message(client, event->channel_id, &create_params, NULL);
    }
  }
  sqlite3_finalize(stmt);
}

int main(void) {
  const char* TOKEN = getenv("TOKEN");
  if (TOKEN == NULL) {
    log_error("Missing TOKEN env variable");
    return 1;
  }

  int status = sqlite3_open("db.db", &DB);
  log_info("Opening database");
  if (status != SQLITE_OK) {
    log_error("Error while opening database: %d\n", status);
    sqlite3_close(DB);
    return 1;
  }
  sqlite3_enable_load_extension(DB, 1);
  sqlite3_load_extension(DB, "./json1", NULL, NULL);

  sqlite3_stmt* stmt;
  sqlite3_prepare(DB, "CREATE TABLE IF NOT EXISTS guilds(id INTEGER NOT NULL PRIMARY KEY, channel INTEGER, last_user INTEGER, last_char INTEGER, started BOOLEAN, typed_words TEXT);", -1, &stmt, NULL);
  status = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (status != SQLITE_DONE) {
    log_error("%d", status);
    sqlite3_close(DB);
    return 1;
  }


  sqlite3_prepare(DB, "CREATE TABLE IF NOT EXISTS words(word TEXT PRIMARY KEY);", -1, &stmt, NULL);
  status = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (status != SQLITE_DONE) {
    log_error("%d", status);
    sqlite3_close(DB);
    return 1;
  }

  sqlite3_prepare(DB, "SELECT word FROM words LIMIT 1;", -1, &stmt, NULL);
  status = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (status == SQLITE_DONE) {
    log_info("Loading dictionary");
    FILE* dict = fopen("words.txt", "r");

    if (dict == NULL) {
      log_error("Failed to load words.txt");
      sqlite3_close(DB);
      return 1;
    }

    char* line = NULL;
    ssize_t read;
    size_t len = 0;

    sqlite3_stmt* stmt;
    while ((read = getline(&line, &len, dict)) != -1) {
      if (line[read - 1] = '\n') {
        line[read - 1] = '\0';
        read--;
      }
      sqlite3_prepare(DB, "INSERT INTO words(word) VALUES(?1)", -1, &stmt, NULL);
      sqlite3_bind_text(stmt, 1, line, -1, SQLITE_STATIC);
      sqlite3_step(stmt);
      sqlite3_finalize(stmt);
    }

    if (line)
      free(line);

  } else if (status == SQLITE_ROW) {
    log_info("dictionary is already loaded");
  }

  struct discord *client = discord_init(TOKEN);
  discord_add_intents(client, DISCORD_GATEWAY_MESSAGE_CONTENT);
  discord_set_on_ready(client, &on_ready);
  discord_set_on_interaction_create(client, &on_interaction_create);
  discord_set_on_message_create(client, &on_message_create);

  log_info("Starting bot");
  discord_run(client);
  sqlite3_close(DB);

  return 0;
}
