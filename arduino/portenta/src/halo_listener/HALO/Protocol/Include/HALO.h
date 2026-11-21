#pragma once

#include "../Private/Application.h"
#include <memory>
#include <string>

namespace HALO {

class ListenerEndpoint {
public:
	ListenerEndpoint();
	friend class Interface;
	bool IsBound() const;
	return_code_t IsMessageAvailable(uint8_t lane);   // RC_OK_TRUE / RC_OK_FALSE
	// Next complete message on the lane, with the arrival time of its last frame and its tag.
	// Message, timestamp and tag always belong to the same message.
	// msg must be initialized with the INIT of the expected type.
	// RC_OK, RC_OK_NO_DATA, ...
	return_code_t Receive(uint8_t lane, void *msg,
	                      uint32_t *timestamp_out = nullptr, uint8_t *tag_out = nullptr);
	return_code_t ReceiveRequestWithTag(uint32_t *timestamp_out, uint8_t *tag_out);
	return_code_t GetControllerTime(uint32_t *time_out);

private:
	void Bind(const halo_channel_t *ch);
	const halo_channel_t *channel;
	bool bound;
};

class ControllerEndpoint {
  public:
	ControllerEndpoint();
	friend class Interface;
	bool IsBound() const;
	return_code_t Request();
	return_code_t Receive(void *msg);
	return_code_t Send(void *msg);

  private:
	void Bind(const halo_channel_t *ch);
	const halo_channel_t *channel;
	bool bound;
};

class ResponderEndpoint {
  public:
	ResponderEndpoint();
	friend class Interface;
	bool IsBound() const;
	return_code_t Respond(void *msg);
	return_code_t Receive(void *msg);
	// Estimated current time (microseconds) in this channel's controller
	// reference frame. RC_OK_NO_DATA if no fresh sync has been received from
	// that controller yet (never synced or offset gone stale).
	return_code_t GetControllerTime(uint32_t *time_out);
		// RC_OK_TRUE once for each response whose transmission has completed (all frames
	// handed to the CAN driver); the flag is consumed by this call.
	// Call it right after Service() and BEFORE Respond(): Respond() clears the flag
	// when the responder is idle, so the opposite order misses completions.
	return_code_t IsResponseDone();

  private:
	void Bind(const halo_channel_t *ch);
	const halo_channel_t *channel;
	bool bound;
};

class Interface {
  public:

	static Interface &Instance() {
		static Interface instance;
		return instance;
	}

	~Interface() = default;

	return_code_t Initialize(const halo_node_t *node);
	void Service(uint8_t steps);

	ListenerEndpoint BindListenerEndpoint(const halo_channel_t *channel);
	ControllerEndpoint BindControllerEndpoint(const halo_channel_t *channel);
	ResponderEndpoint BindResponderEndpoint(const halo_channel_t *channel);
	// External CAN IDs (not belonging to the configured network) seen since the previous
	// call, with the number of frames each sent; the table is cleared by this call.
	// Call once per analysis window. Listener nodes only (RC_PERM_DENIED otherwise).
	// `out` must hold at least `max` entries: PAL_UNKNOWN_ID_MAX is enough.
	return_code_t TakeUnknownIds(pal_unknown_id_t *out, uint8_t max,
	                             uint8_t *count, uint32_t *untracked_frames);

	// Delete copy/move
	Interface(const Interface &) = delete;
	Interface &operator=(const Interface &) = delete;
	Interface(Interface &&) = delete;
	Interface &operator=(Interface &&) = delete;

  private:
	Interface() = default;
	const halo_node_t *node = nullptr;
};

} // namespace HALO