/*
 * Wayland Support
 *
 * Copyright (C) 2012 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include <config.h>

#include <clutter/clutter.h>
#include <clutter/wayland/clutter-wayland-compositor.h>
#include <clutter/wayland/clutter-wayland-surface.h>
#include <stdlib.h>
#include <string.h>
#include <linux/input.h>
#include "meta-wayland-input-device.h"
#include "meta-wayland-stage.h"
#include "meta-wayland-private.h"

struct _MetaWaylandInputDevice
{
  struct wl_input_device parent;

  ClutterActor *stage;
};

static void
input_device_attach (struct wl_client *client,
                     struct wl_resource *resource,
                     uint32_t time,
                     struct wl_resource *buffer_resource,
                     int32_t hotspot_x,
                     int32_t hotspot_y)
{
  MetaWaylandCompositor *compositor = meta_wayland_compositor_get_default ();
  MetaWaylandStage *stage = META_WAYLAND_STAGE (compositor->stage);
  MetaWaylandInputDevice *device = resource->data;
  struct wl_input_device *input_device = (struct wl_input_device *) device;

  if (time < input_device->pointer_focus_time)
    return;
  if (input_device->pointer_focus == NULL)
    return;
  if (input_device->pointer_focus->resource.client != client)
    return;

  if (buffer_resource)
    meta_wayland_stage_set_cursor_from_buffer (stage,
                                               buffer_resource->data,
                                               hotspot_x,
                                               hotspot_y);
  else
    meta_wayland_stage_set_invisible_cursor (stage);
}

const static struct wl_input_device_interface
input_device_interface =
  {
    input_device_attach
  };

static void
unbind_input_device (struct wl_resource *resource)
{
  wl_list_remove (&resource->link);
  free (resource);
}

static void
bind_input_device (struct wl_client *client,
                   void *data,
                   uint32_t version,
                   uint32_t id)
{
  struct wl_input_device *device = data;
  struct wl_resource *resource;

  resource = wl_client_add_object (client,
                                   &wl_input_device_interface,
                                   &input_device_interface,
                                   id,
                                   data);

  wl_list_insert (&device->resource_list, &resource->link);

  resource->destroy = unbind_input_device;
}

MetaWaylandInputDevice *
meta_wayland_input_device_new (struct wl_display *display,
                               ClutterActor *stage)
{
  MetaWaylandInputDevice *device = g_new (MetaWaylandInputDevice, 1);

  wl_input_device_init (&device->parent);
  device->stage = stage;

  wl_display_add_global (display,
                         &wl_input_device_interface,
                         device,
                         bind_input_device);

  return device;
}

static void
handle_motion_event (MetaWaylandInputDevice *input_device,
                     const ClutterMotionEvent *event)
{
  struct wl_input_device *device =
    (struct wl_input_device *) input_device;

  device->x = event->x;
  device->y = event->y;

  meta_wayland_input_device_repick (input_device,
                                    event->time,
                                    event->source);

  device->grab->interface->motion (device->grab,
                                   event->time,
                                   device->grab->x,
                                   device->grab->y);
}

static void
handle_button (MetaWaylandInputDevice *input_device,
               uint32_t time,
               uint32_t button,
               gboolean state)
{
  struct wl_input_device *device =
    (struct wl_input_device *) input_device;

  if (state)
    {
      if (device->button_count == 0)
        {
          struct wl_input_device *wayland_device =
            (struct wl_input_device *)input_device;
          struct wl_surface *wayland_surface = wayland_device->current;
          MetaWaylandSurface *surface;

          device->grab_button = button;
          device->grab_time = time;
          device->grab_x = device->x;
          device->grab_y = device->y;

          if (wayland_surface)
            {
              surface = wayland_surface->resource.data;
              if (surface->window &&
                  surface->window->client_type == META_WINDOW_CLIENT_TYPE_WAYLAND)
                {
                  meta_window_raise (surface->window);
                }
            }
        }

      device->button_count++;
    }
  else
    device->button_count--;

  device->grab->interface->button (device->grab, time, button, state);
}

static void
handle_button_event (MetaWaylandInputDevice *input_device,
                     const ClutterButtonEvent *event)
{
  gboolean state = event->type == CLUTTER_BUTTON_PRESS;
  uint32_t button;

  switch (event->button)
    {
      /* The evdev input right and middle button numbers are swapped
         relative to how Clutter numbers them */
    case 2:
      button = BTN_MIDDLE;
      break;

    case 3:
      button = BTN_RIGHT;
      break;

    default:
      button = event->button + BTN_LEFT - 1;
      break;
    }

  handle_button (input_device, event->time, button, state);
}

static void
handle_key_event (MetaWaylandInputDevice *input_device,
                  const ClutterKeyEvent *event)
{
  struct wl_input_device *device =
    (struct wl_input_device *) input_device;
  gboolean state = event->type == CLUTTER_KEY_PRESS;
  guint evdev_code;

  /* We can't do anything with the event if we can't get an evdev
     keycode for it */
  if (event->device == NULL ||
      !clutter_input_device_keycode_to_evdev (event->device,
                                              event->hardware_keycode,
                                              &evdev_code))
    return;

  /* We want to ignore events that are sent because of auto-repeat. In
     the Clutter event stream these appear as a single key press
     event. We can detect that because the key will already have been
     pressed */
  if (state)
    {
      uint32_t *end = (void *) ((char *) device->keys.data + device->keys.size);
      uint32_t *k;

      /* Ignore the event if the key is already down */
      for (k = device->keys.data; k < end; k++)
        if (*k == evdev_code)
          return;

      /* Otherwise add the key to the list of pressed keys */
      k = wl_array_add (&device->keys, sizeof (*k));
      *k = evdev_code;
    }
  else
    {
      uint32_t *end = (void *) ((char *) device->keys.data + device->keys.size);
      uint32_t *k;

      /* Remove the key from the array */
      for (k = device->keys.data; k < end; k++)
        if (*k == evdev_code)
          {
            *k = *(end - 1);
            device->keys.size -= sizeof (*k);

            goto found;
          }

      g_warning ("unexpected key release event for key 0x%x", evdev_code);

    found:
      (void) 0;
    }

  if (device->keyboard_focus_resource)
    wl_resource_post_event (device->keyboard_focus_resource,
                            WL_INPUT_DEVICE_KEY,
                            event->time,
                            evdev_code,
                            state);
}

static void
handle_scroll_event (MetaWaylandInputDevice *input_device,
                     const ClutterScrollEvent *event)
{
  int button;

  /* Clutter converts the scroll button events into separate scroll
     events. We want to convert these back to button press and release
     events */

  switch (event->direction)
    {
    case CLUTTER_SCROLL_UP:
      button = BTN_SIDE;
      break;

    case CLUTTER_SCROLL_DOWN:
      button = BTN_EXTRA;
      break;

    case CLUTTER_SCROLL_LEFT:
      button = BTN_FORWARD;
      break;

    case CLUTTER_SCROLL_RIGHT:
      button = BTN_BACK;
      break;

    default:
      return;
    }

  /* Synthesize a button press and release */
  handle_button (input_device, event->time, button, TRUE /* press */);
  handle_button (input_device, event->time, button, FALSE /* release */);
}

void
meta_wayland_input_device_handle_event (MetaWaylandInputDevice *input_device,
                                        const ClutterEvent *event)
{
  switch (event->type)
    {
    case CLUTTER_MOTION:
      handle_motion_event (input_device,
                           (const ClutterMotionEvent *) event);
      break;

    case CLUTTER_BUTTON_PRESS:
    case CLUTTER_BUTTON_RELEASE:
      handle_button_event (input_device,
                           (const ClutterButtonEvent *) event);
      break;

    case CLUTTER_KEY_PRESS:
    case CLUTTER_KEY_RELEASE:
      handle_key_event (input_device,
                        (const ClutterKeyEvent *) event);
      break;

    case CLUTTER_SCROLL:
      handle_scroll_event (input_device,
                           (const ClutterScrollEvent *) event);
      break;

    default:
      break;
    }
}

/* The actor argument can be NULL in which case a Clutter pick will be
   performed to determine the right actor. An actor should only be
   passed if the repick is being performed due to an event in which
   case Clutter will have already performed a pick so we can avoid
   redundantly doing another one */
void
meta_wayland_input_device_repick (MetaWaylandInputDevice *device,
                                  uint32_t                time,
                                  ClutterActor           *actor)
{
  struct wl_input_device *input_device = (struct wl_input_device *) device;
  struct wl_surface *surface;
  MetaWaylandSurface *focus;

  if (actor == NULL)
    {
      ClutterStage *stage = CLUTTER_STAGE (device->stage);
      actor = clutter_stage_get_actor_at_pos (stage,
                                              CLUTTER_PICK_REACTIVE,
                                              input_device->x, input_device->y);
    }

  if (CLUTTER_WAYLAND_IS_SURFACE (actor))
    {
      ClutterWaylandSurface *wl_surface = CLUTTER_WAYLAND_SURFACE (actor);
      float ax, ay;

      clutter_actor_transform_stage_point (actor,
                                           input_device->x, input_device->y,
                                           &ax, &ay);
      input_device->current_x = ax;
      input_device->current_y = ay;

      surface = clutter_wayland_surface_get_surface (wl_surface);
    }
  else
    surface = NULL;

  if (surface != input_device->current)
    {
      const struct wl_grab_interface *interface = input_device->grab->interface;
      interface->focus (input_device->grab, time, surface,
                        input_device->current_x, input_device->current_y);
      input_device->current = surface;
    }

  focus = (MetaWaylandSurface *) input_device->grab->focus;
  if (focus)
    {
      float ax, ay;

      clutter_actor_transform_stage_point (focus->actor,
                                           input_device->x, input_device->y,
                                           &ax, &ay);
      input_device->grab->x = ax;
      input_device->grab->y = ay;
    }
}