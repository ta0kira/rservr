#include "client.h"

#include "load-all.h"
#include "python-macro.h"


#define ALL_GLOBAL_BINDINGS(macro) \
macro(initialize_client) \
macro(client_cleanup) \
macro(isolate_client) \
macro(check_ipc_status) \
macro(deregister_client) \
macro(client_message) \
macro(ping_client) \
macro(ping_server) \
macro(short_response) \
macro(client_response) \
macro(client_response_list) \
macro(get_client_name) \
macro(get_client_type) \
macro(get_server_name) \
macro(request_terminal) \
macro(return_terminal) \
macro(terminal_control) \
macro(disable_indicate_ready) \
macro(manual_indicate_ready)



GLOBAL_BINDING_START(initialize_client, "")
	NO_ARGUMENTS
	if (!initialize_client()) return auto_exception(PyExc_IOError, "");
	NO_RETURN
GLOBAL_BINDING_END(initialize_client)



GLOBAL_BINDING_START(client_cleanup, "")
	NO_ARGUMENTS
	client_cleanup();
	NO_RETURN
GLOBAL_BINDING_END(client_cleanup)



GLOBAL_BINDING_START(isolate_client, "")
	NO_ARGUMENTS
	isolate_client();
	NO_RETURN
GLOBAL_BINDING_END(isolate_client)



GLOBAL_BINDING_START(check_ipc_status, "")
	NO_ARGUMENTS
	return Py_BuildValue("i", check_ipc_status());
GLOBAL_BINDING_END(check_ipc_status)



GLOBAL_BINDING_START(deregister_client, "")
	NO_ARGUMENTS
	if (!deregister_client()) return auto_exception(PyExc_RuntimeError, "");
	NO_RETURN
GLOBAL_BINDING_END(deregister_client)



GLOBAL_BINDING_START(client_message, "")
	STATIC_KEYWORDS(keywords) = { "target", "data", NULL };
	const char *target = NULL, *data = NULL;
	if(!PyArg_ParseTupleAndKeywords(ARGS, KEYWORDS, "ss", keywords, &target, &data)) return NULL;
	return new_handle_instance("command_handle", client_message(target, data));
GLOBAL_BINDING_END(client_message)



GLOBAL_BINDING_START(ping_client, "")
	STATIC_KEYWORDS(keywords) = { "target", NULL };
	const char *target = NULL;
	if(!PyArg_ParseTupleAndKeywords(ARGS, KEYWORDS, "s", keywords, &target)) return NULL;
	return new_handle_instance("command_handle", ping_client(target));
GLOBAL_BINDING_END(ping_client)



GLOBAL_BINDING_START(ping_server, "")
	NO_ARGUMENTS
	return new_handle_instance("command_handle", ping_server());
GLOBAL_BINDING_END(ping_server)



GLOBAL_BINDING_START(short_response, "")
	STATIC_KEYWORDS(keywords) = { "handle", "event", NULL };
	PyObject *object = NULL;
	long event = 0;
	if(!PyArg_ParseTupleAndKeywords(ARGS, KEYWORDS, "Oi", keywords, &object, &event)) return NULL;
	if (!check_instance("message_info", object) && !check_instance("message_handle", object)) return NULL;
	long message = 0;
	if (!py_to_long(&message, object)) return NULL;
	return new_handle_instance("command_handle", short_response((message_handle) (void*) message, event));
GLOBAL_BINDING_END(short_response)



GLOBAL_BINDING_START(client_response, "")
	STATIC_KEYWORDS(keywords) = { "handle", "event", "data", NULL };
	PyObject *object = NULL;
	long event = 0;
	const char *data = NULL;
	if(!PyArg_ParseTupleAndKeywords(ARGS, KEYWORDS, "Ois", keywords, &object, &event, &data)) return NULL;
	if (!check_instance("message_info", object) && !check_instance("message_handle", object)) return NULL;
	long message = 0;
	if (!py_to_long(&message, object)) return NULL;
	return new_handle_instance("command_handle", client_response((message_handle) (void*) message, event, data));
GLOBAL_BINDING_END(client_response)



GLOBAL_BINDING_START(client_response_list, "")
	NOT_IMPLEMENTED
GLOBAL_BINDING_END(client_response_list)



GLOBAL_BINDING_START(get_client_name, "")
	NO_ARGUMENTS
	return Py_BuildValue("s", get_client_name());
GLOBAL_BINDING_END(get_client_name)



GLOBAL_BINDING_START(get_client_type, "")
	NO_ARGUMENTS
	return Py_BuildValue("l", (long) get_client_type());
GLOBAL_BINDING_END(get_client_type)



GLOBAL_BINDING_START(get_server_name, "")
	NO_ARGUMENTS
	return Py_BuildValue("s", get_server_name());
GLOBAL_BINDING_END(get_server_name)



GLOBAL_BINDING_START(request_terminal, "")
	NO_ARGUMENTS
	int descriptor = -1;
	if (!request_terminal(&descriptor)) return auto_exception(PyExc_RuntimeError, "");
	return Py_BuildValue("i", descriptor);
GLOBAL_BINDING_END(request_terminal)



GLOBAL_BINDING_START(return_terminal, "")
	NO_ARGUMENTS
	if (!return_terminal()) return auto_exception(PyExc_RuntimeError, "");
	NO_RETURN
GLOBAL_BINDING_END(return_terminal)



GLOBAL_BINDING_START(terminal_control, "")
	NO_ARGUMENTS
	return Py_BuildValue("i", terminal_control());
GLOBAL_BINDING_END(terminal_control)



GLOBAL_BINDING_START(disable_indicate_ready, "")
	NO_ARGUMENTS
	disable_indicate_ready();
	NO_RETURN
GLOBAL_BINDING_END(disable_indicate_ready)



GLOBAL_BINDING_START(manual_indicate_ready, "")
	NO_ARGUMENTS
	if (!manual_indicate_ready()) return auto_exception(PyExc_IOError, "");
	NO_RETURN
GLOBAL_BINDING_END(manual_indicate_ready)



int python_load_client(PyObject *MODULE)
{
	ALL_GLOBAL_BINDINGS(LOAD_GLOBAL_BINDING)
	return 1;
}