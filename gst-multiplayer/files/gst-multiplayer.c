#include <gst/gst.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <pthread.h>

// Set number of streams configurable for scalability
#define NUM_PLAYERS 2

// Set window coordinates
#define FIRST_WINDOW_X 0
#define SECOND_WINDOW_X 920
#define WINDOW_Y 290
#define PRIMARY_WINDOW_WIDTH 900
#define PRIMARY_WINDOW_HEIGHT 500
#define SECONDARY_WINDOW_WIDTH 720
#define SECONDARY_WINDOW_HEIGHT 400

typedef struct
{
  GstElement *pipeline;
  GstElement *playbin;
  GstElement *video_sink;
  GstElement *audio_sink;
  GstElement *audio_bin;
  GstElement *valve;
  GstPad *ghostpad;
  GstBus *bus;
  char *uri;
  char primary_coord[32];
  char secondary_coord[32];
} VideoPlayer;

static VideoPlayer player[NUM_PLAYERS];
GMainLoop *loop;
gboolean ping_pong = TRUE;
int primary_player_idx = 0;

// Query current position from pipeline and send it to audiosink
void send_segment_event(int player_index)
{
  GstSegment *segment;
  GstEvent *segment_event;
  GstFormat format = GST_FORMAT_TIME;
  gint64 current_position;

  // Query the current position from the playbin
  if (gst_element_query_position(player[player_index].playbin, format, &current_position))
  {
    g_print("Current position: %" GST_TIME_FORMAT "\n", GST_TIME_ARGS(current_position));
  }
  else
  {
    g_print("Failed to query position, using 0 as fallback\n");
    current_position = 0; // Fallback if query fails
  }

  // Create and configure a new segment
  segment = gst_segment_new();
  gst_segment_init(segment, GST_FORMAT_TIME);
  segment->start = current_position; // Start from the current playback position
  segment->position = current_position;
  segment->rate = 1.0;              // Normal playback rate
  segment->time = current_position; // Base time for the segment
  segment->stop = -1;               // No stop time (play until end)

  // Send the segment event to the audiosink
  segment_event = gst_event_new_segment(segment);
  GstPad *sink_pad = gst_element_get_static_pad(player[player_index].audio_sink, "sink");
  if (!sink_pad)
  {
    g_printerr("Failed to get sink pad of audiosink.\n");
    return;
  }
  if (!gst_pad_send_event(sink_pad, segment_event))
  {
    g_print("Failed to send segment event to sink pad\n");
  }
  // Clean up
  gst_segment_free(segment);
  gst_object_unref(sink_pad);
}

static GstBusSyncReply bus_sync_handler(GstBus *bus, GstMessage *msg, gpointer user_data)
{
  VideoPlayer *player_this = (VideoPlayer *)user_data;

  switch (GST_MESSAGE_TYPE(msg))
  {
  case GST_MESSAGE_EOS:
    g_print("End of Stream (EOS) message received\n");
    g_main_loop_quit(loop);
    break;
  case GST_MESSAGE_ERROR:
  {
    GError *err = NULL;
    gchar *name, *debug = NULL;

    name = gst_object_get_path_string(msg->src);
    gst_message_parse_error(msg, &err, &debug);

    g_printerr("ERROR: from element %s: %s\n", name, err->message);
    if (debug != NULL)
      g_printerr("Additional debug info:\n%s\n", debug);

    g_error_free(err);
    g_free(debug);
    g_free(name);

    g_main_loop_quit(loop);
    break;
  }
  case GST_MESSAGE_WARNING:
  {
    GError *err = NULL;
    gchar *name, *debug = NULL;

    name = gst_object_get_path_string(msg->src);
    gst_message_parse_warning(msg, &err, &debug);

    g_printerr("ERROR: from element %s: %s\n", name, err->message);
    if (debug != NULL)
      g_printerr("Additional debug info:\n%s\n", debug);

    g_error_free(err);
    g_free(debug);
    g_free(name);
    break;
  }
  default:
    break;
  }

  return GST_BUS_PASS; /* pass the message to the async queue */
}

static void switch_primary()
{
  int secondary_player_idx = 1;

  // Toggle primary video since we now have only two streams
  if (ping_pong == TRUE)
  {
    primary_player_idx = 1;
    secondary_player_idx = 0;
    ping_pong = FALSE;
  }
  else
  {
    primary_player_idx = 0;
    secondary_player_idx = 1;
    ping_pong = TRUE;
  }

  // Resize secondary stream to be smaller, and drop buffers
  g_object_set(player[secondary_player_idx].valve, "drop", TRUE, NULL);
  g_object_set(player[secondary_player_idx].video_sink, "window-set", player[secondary_player_idx].secondary_coord, NULL);
  g_object_set(player[secondary_player_idx].video_sink, "rectangle", player[secondary_player_idx].secondary_coord, NULL);

  // Enlarge primary stream, send segment event to sync audio. Start allowing buffers to pass through
  g_object_set(player[primary_player_idx].video_sink, "window-set", player[primary_player_idx].primary_coord, NULL);
  g_object_set(player[primary_player_idx].video_sink, "rectangle", player[primary_player_idx].primary_coord, NULL);

  send_segment_event(primary_player_idx);
  g_object_set(player[primary_player_idx].valve, "drop", FALSE, NULL);

  g_print("player_%d : switched to primary audio \n", primary_player_idx);
}

// Test function to toggle primary stream
void *wait_for_user_input(void *dummy)
{
  char user_input;
  while (1)
  {
    printf("Press 'S' to switch streams, 'X' to exit the program: ");
    user_input = getchar();
    getchar(); // Consume newline character after input

    if (user_input == 'S' || user_input == 's')
    {
      switch_primary();
    }
    else if (user_input == 'X' || user_input == 'x')
    {
      printf("Exiting the program...\n");
      break;
    }
    else
    {
      printf("Invalid input. Please press 'S' or 'X'.\n");
    }
  }
}

// Create player in common function
static void *play_video(void *data)
{
  VideoPlayer *player_instance = (VideoPlayer *)data;

  // Create playbin, sinks and other elements
  player_instance->pipeline = gst_pipeline_new("custom-pipeline");
  player_instance->playbin = gst_element_factory_make("playbin", "playbin");
  player_instance->video_sink = gst_element_factory_make("westerossink", "westerossink");
  player_instance->audio_sink = gst_element_factory_make("amlhalasink", "autoaudiosink");
  player_instance->valve = gst_element_factory_make("valve", "valve");
  player_instance->audio_bin = gst_bin_new("custom-bin");

  if (!player_instance->pipeline || !player_instance->playbin || !player_instance->audio_sink || !player_instance->audio_bin || !player_instance->video_sink || !player_instance->valve)
  {
    g_printerr("Failed to create elements\n");
    return NULL;
  }

  // Add audio elements to the bin
  gst_bin_add_many(GST_BIN(player_instance->audio_bin), player_instance->valve, player_instance->audio_sink, NULL);

  // Set up properties for audio and video sinks
  g_object_set(player_instance->audio_sink, "sync", TRUE, NULL);
  g_object_set(player_instance->video_sink, "sync", FALSE, NULL);
  g_object_set(player_instance->audio_sink, "seamless-switch", TRUE, NULL);
  g_object_set(player_instance->playbin, "uri", player_instance->uri, NULL);

  // Set up ghost pad for linking the audio bin
  player_instance->ghostpad = gst_ghost_pad_new("sink", gst_element_get_static_pad(player_instance->valve, "sink"));
  gst_pad_set_active(player_instance->ghostpad, TRUE);
  gst_element_add_pad(player_instance->audio_bin, player_instance->ghostpad);

  if (!gst_element_link_many(player_instance->valve, player_instance->audio_sink, NULL))
  {
    g_print("Could not link valve and audio sink \n");
  }
  g_object_set(player_instance->playbin, "audio-sink", player_instance->audio_bin, NULL);
  g_object_set(player_instance->playbin, "video-sink", player_instance->video_sink, NULL);

  gst_bin_add_many(GST_BIN(player_instance->pipeline), player_instance->playbin, NULL);

  // Set window coordinates based on player instance
  if (player_instance == &player[0])
  {
    g_object_set(player_instance->valve, "drop", FALSE, NULL);
    g_object_set(player_instance->video_sink, "window-set", player_instance->primary_coord, NULL);
    g_object_set(player_instance->video_sink, "rectangle", player_instance->primary_coord, NULL);
  }
  else
  {
    g_object_set(player_instance->video_sink, "res-usage", 0x00000005, NULL);
    g_object_set(player_instance->video_sink, "window-set", player_instance->secondary_coord, NULL);
    g_object_set(player_instance->video_sink, "rectangle", player_instance->secondary_coord, NULL);
    g_object_set(player_instance->valve, "drop", TRUE, NULL);
  }

  // Set up the bus and start playing the video
  player_instance->bus = gst_element_get_bus(player_instance->pipeline);
  gst_bus_set_sync_handler(player_instance->bus, (GstBusSyncHandler)bus_sync_handler, player_instance, NULL); /* Assigns a synchronous bus_sync_handler for synchronous messages */

  gst_element_set_state(player_instance->pipeline, GST_STATE_PLAYING);
}

// Main function to initialize the video players and run the loop
int main(int argc, char *argv[])
{
  pthread_t thread1, thread2, thread3;
  if (argc != 3)
  {
    printf("Usage: %s <URI 1> <URI 2>\n", argv[0]);
    printf("Provide two URIs as command-line arguments.\n");
    printf("Press 'S' to switch streams, 'X' to exit the program: \n");
    return -1;
  }

  gst_init(&argc, &argv);
  // Create the main loop
  loop = g_main_loop_new(NULL, FALSE);

  player[0].uri = argv[1];
  player[1].uri = argv[2];

  // Initialize primary and secondary coordinates for window positions
  snprintf(player[0].primary_coord, sizeof(player[0].primary_coord), "%d,%d,%d,%d", FIRST_WINDOW_X, WINDOW_Y, PRIMARY_WINDOW_WIDTH, PRIMARY_WINDOW_HEIGHT);
  snprintf(player[1].primary_coord, sizeof(player[1].primary_coord), "%d,%d,%d,%d", SECOND_WINDOW_X, WINDOW_Y, PRIMARY_WINDOW_WIDTH, PRIMARY_WINDOW_HEIGHT);
  snprintf(player[0].secondary_coord, sizeof(player[0].secondary_coord), "%d,%d,%d,%d", FIRST_WINDOW_X, WINDOW_Y, SECONDARY_WINDOW_WIDTH, SECONDARY_WINDOW_HEIGHT);
  snprintf(player[1].secondary_coord, sizeof(player[1].secondary_coord), "%d,%d,%d,%d", SECOND_WINDOW_X, WINDOW_Y, SECONDARY_WINDOW_WIDTH, SECONDARY_WINDOW_HEIGHT);

  // Create two threads for two pipelines
  if (pthread_create(&thread2, NULL, play_video, (void *)&player[1]))
  {
    perror("Failed to create thread for player 1");
    return 1;
  }

  if (pthread_create(&thread1, NULL, play_video, (void *)&player[0]))
  {
    perror("Failed to create thread for player 2");
    return 1;
  }

  // Handle user input for testing
  pthread_create(&thread3, NULL, wait_for_user_input, NULL);

  // Run the main loop
  g_main_loop_run(loop);

  // Wait for threads to finish
  pthread_join(thread1, NULL);
  pthread_join(thread2, NULL);
  pthread_join(thread3, NULL);

  // Cleanup
  g_main_loop_unref(loop);
  gst_element_set_state(player[0].pipeline, GST_STATE_NULL);
  gst_object_unref(player[0].pipeline);
  gst_object_unref(player[0].bus);
  gst_element_set_state(player[1].pipeline, GST_STATE_NULL);
  gst_object_unref(player[1].pipeline);
  gst_object_unref(player[1].bus);

  return 0;
}