#include "../Include/HALO.h"
#include <iostream>

//HALO::ListenerEndpoint implementation

HALO::ListenerEndpoint::ListenerEndpoint() : channel(nullptr), bound(false) {}
void HALO::ListenerEndpoint::Bind(const halo_channel_t *ch) { channel = ch; bound = (ch != nullptr); }
bool HALO::ListenerEndpoint::IsBound() const { return bound; }

return_code_t HALO::ListenerEndpoint::IsMessageAvailable(uint8_t lane) {
	if (!bound) { std::cerr << "Error: ListenerEndpoint not bound." << std::endl; return RC_FATAL; }
	return Listener_Is_Msg_Available(channel, lane);
}
return_code_t HALO::ListenerEndpoint::Receive(uint8_t lane, void *msg,
                                              uint32_t *timestamp_out, uint8_t *tag_out) {
	if (!bound) { std::cerr << "Error: ListenerEndpoint not bound." << std::endl; return RC_FATAL; }
	return Listener_Receive(channel, lane, msg, timestamp_out, tag_out);
}
return_code_t HALO::ListenerEndpoint::ReceiveRequestWithTag(uint32_t *timestamp_out, uint8_t *tag_out) {
	if (!bound) { std::cerr << "Error: ListenerEndpoint not bound." << std::endl; return RC_FATAL; }
	return Listener_Receive_Request_With_Tag(channel, timestamp_out, tag_out);
}

return_code_t HALO::ListenerEndpoint::GetControllerTime(uint32_t *time_out) {
	if (!bound) { std::cerr << "Error: ListenerEndpoint not bound." << std::endl; return RC_FATAL; }
	return Listener_Get_Controller_Time(channel, time_out);
}

HALO::ListenerEndpoint HALO::Interface::BindListenerEndpoint(const halo_channel_t *channel) {
	ListenerEndpoint ep;
	if (this->node == nullptr || channel == nullptr) { std::cerr << "Error: node not initialized or null channel." << std::endl; return ep; }
	if (channel->controller->id == this->node->id || channel->responder->id == this->node->id) {
		std::cerr << "Error: this node is controller or responder for the channel." << std::endl;
		return ep;
	}
	ep.Bind(channel);
	return ep;
}

//
// HALO::ControllerEndpoint implementation
//

HALO::ControllerEndpoint::ControllerEndpoint()
    : channel(nullptr), bound(false)
{
}

void HALO::ControllerEndpoint::Bind(const halo_channel_t* ch)
{
    channel = ch;
    bound = (ch != nullptr);
}

bool HALO::ControllerEndpoint::IsBound() const
{
    return bound;
}

return_code_t HALO::ControllerEndpoint::Request()
{
    if (!bound) {
        std::cerr << "Error: ControllerEndpoint not bound. Call BindControllerEndpoint() first." << std::endl;
        return RC_FATAL;
    }
    return Controller_Request(channel);
}

return_code_t HALO::ControllerEndpoint::Receive(void* msg)
{
    if (!bound) {
        std::cerr << "Error: ControllerEndpoint not bound. Call BindControllerEndpoint() first." << std::endl;
        return RC_FATAL;
    }
    return Controller_Receive(channel, msg);
}

return_code_t HALO::ControllerEndpoint::Send(void* msg)
{
    if (!bound) {
        std::cerr << "Error: ControllerEndpoint not bound. Call BindControllerEndpoint() first." << std::endl;
        return RC_FATAL;
    }
    return Controller_Send(msg, channel);
}


//
// HALO::ResponderEndpoint implementation
//

HALO::ResponderEndpoint::ResponderEndpoint()
    : channel(nullptr), bound(false)
{
}

void HALO::ResponderEndpoint::Bind(const halo_channel_t* ch)
{
    channel = ch;
    bound = (ch != nullptr);
}

bool HALO::ResponderEndpoint::IsBound() const
{
    return bound;
}

return_code_t HALO::ResponderEndpoint::Respond(void* msg)
{
    if (!bound) {
        std::cerr << "Error: ResponderEndpoint not bound. Call BindResponderEndpoint() first." << std::endl;
        return RC_FATAL;
    }
    return Responder_Respond(channel, msg);
}

return_code_t HALO::ResponderEndpoint::IsResponseDone()
{
    if (!bound) {
        std::cerr << "Error: ResponderEndpoint not bound. Call BindResponderEndpoint() first." << std::endl;
        return RC_FATAL;
    }
    return Responder_Respond_Is_Done(channel);
}

return_code_t HALO::ResponderEndpoint::Receive(void* msg)
{
    if (!bound) {
        std::cerr << "Error: ResponderEndpoint not bound. Call BindResponderEndpoint() first." << std::endl;
        return RC_FATAL;
    }
    return Responder_Receive(channel, msg);
}

return_code_t HALO::ResponderEndpoint::GetControllerTime(uint32_t* time_out)
{
    if (!bound) {
        std::cerr << "Error: ResponderEndpoint not bound. Call BindResponderEndpoint() first." << std::endl;
        return RC_FATAL;
    }
    return Responder_Get_Controller_Time(channel, time_out);
}



//
// HALO implementation
//

return_code_t HALO::Interface::Initialize(const halo_node_t *node)
{
    this->node = node;
    return Application_Init(node);
}

void HALO::Interface::Service(uint8_t steps)
{
    if(steps == 0) {
        std::cerr << "Warning: Service called with 0 steps. Defaulting to 1 step." << std::endl;
        steps = 1;
    }
    Application_Service(steps);
}

HALO::ControllerEndpoint HALO::Interface::BindControllerEndpoint(const halo_channel_t *channel)
{
    ControllerEndpoint ep; // default-constructed: unbound

    if(this->node == nullptr) {
        std::cerr << "Error: HALO node not initialized. Call Initialize() before binding channels." << std::endl;
        return ep;
    }

    if(channel == nullptr) {
        std::cerr << "Error: Channel pointer is null." << std::endl;
        return ep;
    }

    if(channel->controller->id != this->node->id) {
        std::cerr << "Error: This node is not the controller for the specified channel." << std::endl;
        return ep; // leave unbound: a non-controller node cannot obtain a usable controller endpoint
    }

    ep.Bind(channel); // Interface is friend, allowed to call private Bind
    return ep;
}

HALO::ResponderEndpoint HALO::Interface::BindResponderEndpoint(const halo_channel_t *channel)
{
    ResponderEndpoint ep; // default-constructed: unbound

    if(this->node == nullptr) {
        std::cerr << "Error: HALO node not initialized. Call Initialize() before binding channels." << std::endl;
        return ep;
    }

    if(channel == nullptr) {
        std::cerr << "Error: Channel pointer is null." << std::endl;
        return ep;
    }

    if(channel->responder->id != this->node->id) {
        std::cerr << "Error: This node is not the responder for the specified channel." << std::endl;
        return ep; // leave unbound: a non-responder node cannot obtain a usable responder endpoint
    }

    ep.Bind(channel); // Interface is friend, allowed to call private Bind
    return ep;
}

return_code_t HALO::Interface::TakeUnknownIds(pal_unknown_id_t *out, uint8_t max,
                                              uint8_t *count, uint32_t *untracked_frames)
{
    if (this->node == nullptr) {
        std::cerr << "Error: HALO node not initialized. Call Initialize() first." << std::endl;
        return RC_FATAL;
    }
    return Listener_Take_Unknown_Ids(out, max, count, untracked_frames);
}