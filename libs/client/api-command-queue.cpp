/* This software is released under the BSD License.
 |
 | Copyright (c) 2012, Kevin P. Barry [the resourcerver project]
 | All rights reserved.
 |
 | Redistribution  and  use  in  source  and   binary  forms,  with  or  without
 | modification, are permitted provided that the following conditions are met:
 |
 | - Redistributions of source code must retain the above copyright notice, this
 |   list of conditions and the following disclaimer.
 |
 | - Redistributions in binary  form must reproduce the  above copyright notice,
 |   this list  of conditions and the following disclaimer in  the documentation
 |   and/or other materials provided with the distribution.
 |
 | - Neither the  name  of  the  Resourcerver  Project  nor  the  names  of  its
 |   contributors may be  used to endorse or promote products  derived from this
 |   software without specific prior written permission.
 |
 | THIS SOFTWARE IS  PROVIDED BY THE COPYRIGHT HOLDERS AND  CONTRIBUTORS "AS IS"
 | AND ANY  EXPRESS OR IMPLIED  WARRANTIES,  INCLUDING, BUT  NOT LIMITED TO, THE
 | IMPLIED WARRANTIES OF  MERCHANTABILITY  AND FITNESS FOR A  PARTICULAR PURPOSE
 | ARE DISCLAIMED.  IN  NO EVENT SHALL  THE COPYRIGHT  OWNER  OR CONTRIBUTORS BE
 | LIABLE  FOR  ANY  DIRECT,   INDIRECT,  INCIDENTAL,   SPECIAL,  EXEMPLARY,  OR
 | CONSEQUENTIAL   DAMAGES  (INCLUDING,  BUT  NOT  LIMITED  TO,  PROCUREMENT  OF
 | SUBSTITUTE GOODS OR SERVICES;  LOSS  OF USE,  DATA,  OR PROFITS;  OR BUSINESS
 | INTERRUPTION)  HOWEVER  CAUSED  AND ON  ANY  THEORY OF LIABILITY,  WHETHER IN
 | CONTRACT,  STRICT  LIABILITY, OR  TORT (INCLUDING  NEGLIGENCE  OR  OTHERWISE)
 | ARISING IN ANY  WAY OUT OF  THE USE OF THIS SOFTWARE, EVEN  IF ADVISED OF THE
 | POSSIBILITY OF SUCH DAMAGE.
 +~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

//TODO: review and clean up

#include "api-command-queue.hpp"

#include "plugin-dev/manual-command.hpp"
#include "remote/external-buffer.hpp"

extern "C" {
#include "api/service-client.h"
#include "api/resource-client.h"
}

#include <unistd.h> //'nanosleep'
#include <string.h> //'strlen'

#include "external/global-sentry-pthread.hpp"

#include "api-client.hpp"
#include "api-message-queue.hpp"
#include "api-client-timing.hpp"
#include "client-response-macro.hpp"
#include "client-output.hpp"
#include "client-command.hpp"
#include "command-queue.hpp"
#include "protocol/conversion.hpp"
#include "protocol/constants.hpp"
#include "protocol/ipc/common-input.hpp"
#include "protocol/ipc/cstring-input.hpp"

extern "C" {
#include "protocol/api-label-check.h"
#include "protocol/local-types.h"
#include "protocol/api-timing.h"
#include "lang/translation.h"
}

static protect::literal_capsule <command_status_list, global_sentry_pthread <> > internal_status_list;
protected_command_status_list *const client_command_status = &internal_status_list;


bool update_status(command_reference rReference, command_event eEvent,
const command_info *iInfo)
{
	protected_command_status_list::write_object object = client_command_status->writable();
	if (!object) return false;

	int position = object->f_find(rReference, &command_status_list::find_by_key);

	if (position == data::not_found) return false;

	if ( object->get_element(position).value().update_status(rReference,
	       eEvent, iInfo) )
	object->remove_single(position);

	return true;
}



static protect::literal_capsule <command_queue, global_sentry_pthread <> > queued_client_commands;


command_reference manual_message_number()
{
	static command_reference internal_reference = 1;
	return internal_reference++;
}



const command_transmit *register_new_command(command_transmit *cCommand)
{
	if (!cCommand) return NULL;
	return queue_new_command(&queued_client_commands, cCommand);
}


static void update_status_mask(command_event &sStatus, command_event eEvent)
{ sStatus |= eEvent; }


	command_status::command_status() : local_status() { }

	bool command_status::update_status(command_reference rReference, command_event eEvent,
	const command_info *iInfo)
	{
	bool remove_status = false;

	for (unsigned int I = 0; I < local_callbacks.size();)
	{
	if (local_callbacks[I](rReference, eEvent, local_status, iInfo))
	 {
	local_callbacks.erase(local_callbacks.begin() + I);
	if (!local_callbacks.size()) remove_status = true;
	 }
	else ++I;
	}

	update_status_mask(local_status, eEvent);
	return remove_status;
	}

	bool command_status::register_functors(const event_functor_list &fFunctors)
	{
	for (int I = 0; I < (signed) fFunctors.size(); I++)
	local_callbacks.push_back(fFunctors[I]);
	return true;
	}



bool ATTR_INT register_reference(protected_command_status_list *lList,
command_reference rReference)
{
	if (!lList) return false;
	protected_command_status_list::write_object object = lList->writable();
	if (!object) return false;

	object->add_element( object->new_element(rReference, command_status()) );
	object->f_remove_duplicates(&command_status_list::sort_by_key,
	  &command_status_list::find_dup_key);

	return true;
}



bool ATTR_INT deregister_reference(protected_command_status_list *lList,
command_reference rReference)
{
	if (!lList) return false;
	protected_command_status_list::write_object object = lList->writable();
	if (!object) return false;

	object->f_remove_pattern(rReference, &command_status_list::find_by_key);

	return true;
}



bool ATTR_INT register_functor(protected_command_status_list *lList,
command_reference rReference, const event_functor_list *fFunctor)
{
	if (!lList) return false;
	protected_command_status_list::write_object object = lList->writable();
	if (!object) return false;

	int position = object->f_find(rReference, &command_status_list::find_by_key);

	if (position == data::not_found) return false;

	return object->get_element(position).value().register_functors(*fFunctor);
}



command_event ATTR_INT retrieve_status(protected_command_status_list *lList,
command_reference rReference)
{
	if (!lList) return event_unavailable;
	protected_command_status_list::write_object object = lList->writable();
	if (!object) return event_unavailable;

	int position = object->f_find(rReference, &command_status_list::find_by_key);

	if (position == data::not_found) return event_unavailable;

	return object->get_element(position).value().get_status();
}



bool manual_command_status(command_reference rReference)
{ return register_reference(client_command_status, rReference); }


result change_command_priority(command_handle cCommand, command_priority pPriority)
{ return change_priority(&queued_client_commands, cCommand, pPriority); }


command_reference send_command(command_handle cCommand)
{ return send_command_callbacks(cCommand, NULL); }


command_reference send_command_no_status(command_handle cCommand)
{
	if (!cCommand) return 0;

	if (check_su())
	{
    log_client_send_error(error_su_violation);
	return 0;
	}

	if (!message_queue_status())
	{
    log_client_send_error(message_loop_error);
	return 0;
	}

	command_reference next_reference = manual_message_number();
	if (!next_reference) return 0;

	reset_input_standby();

	return transmit_command(&queued_client_commands, cCommand, pipe_output, next_reference, true)?
	next_reference : 0;
}


command_reference send_command_functor(command_handle cCommand, event_functor *fFunctor)
{
	event_functor_list functors;
	if (fFunctor) functors.push_back(fFunctor);
	return send_command_functors(cCommand, functors);
}


command_reference send_command_functors(command_handle cCommand,
  const event_functor_list &fFunctors)
{
	if (!cCommand) return 0;

	if (check_su())
	{
    log_client_send_error(error_su_violation);
	return 0;
	}

	if (!message_queue_status())
	{
    log_client_send_error(message_loop_error);
	return 0;
	}

	command_reference next_reference = manual_message_number();
	if (!next_reference) return 0;

	register_reference(client_command_status, next_reference);

	if (fFunctors.size())
	register_functor(client_command_status, next_reference, &fFunctors);

	reset_input_standby();

	if (!transmit_command(&queued_client_commands, cCommand, pipe_output, next_reference, false))
	{
	update_status(next_reference, event_unsent, NULL);
	deregister_reference(client_command_status, next_reference);
	return 0;
	}

	return next_reference;
}



command_reference send_command_callback(command_handle cCommand, event_callback cCallback)
{
	event_callback single_callback[] = {cCallback, NULL };
	return send_command_callbacks(cCommand, single_callback);
}


command_reference send_command_callbacks(command_handle cCommand,
const event_callback *cCallback)
{
	event_functor_list functors;
	const event_callback *current = cCallback;
	if (current) while (*current) functors.push_back( encapsulate_callback(*current++) );
	return send_command_functors(cCommand, functors);
}


result new_status_functor(command_reference rReference, event_functor *fFunctor)
{
	event_functor_list functors;
	if (fFunctor) functors.push_back(fFunctor);
	return new_status_functors(rReference, functors);
}


result new_status_functors(command_reference rReference,
  const event_functor_list &fFunctors)
{
	return fFunctors.size()?
	  register_functor(client_command_status, rReference, &fFunctors) : false;
}


result new_status_callback(command_reference rReference, event_callback cCallback)
{
	event_callback single_callback[] = {cCallback, NULL };
	return new_status_callbacks(rReference, single_callback);
}


result new_status_callbacks(command_reference rReference,
const event_callback *cCallback)
{
	event_functor_list functors;
	const event_callback *current = cCallback;
	if (current) while (*current) functors.push_back( encapsulate_callback(*current++) );
	return new_status_functors(rReference, functors);
}


result destroy_command(command_handle cCommand)
{ return remove_command(&queued_client_commands, cCommand); }


command_event wait_command_event(command_reference rReference, command_event sStatus,
long_time wWait)
{ return cancelable_wait_command_event(rReference, sStatus, wWait, NULL); }


command_event cancelable_wait_command_event(command_reference rReference, command_event sStatus,
long_time wWait, int(*cCallback)(command_reference, command_event))
{
	//TODO: make this block if the timeout is zero and no callback is used?

	if (!rReference) return event_unsent;

	if (calling_from_message_queue())
	{
    log_client_recursive_command_wait();
	return event_unavailable;
	}

	//this needs to stay after 'calling_from_message_queue'
	if (message_queue_pause_state()) return event_unavailable;

	struct timespec execute_cycle = client_timing_specs->command_status_retry;
	double total_cycle  = 0;
	double cycle_elapse = (double) execute_cycle.tv_sec +
	  (double) execute_cycle.tv_nsec / (1000.0 * 1000.0 * 1000.0);

	command_event current_status = event_unavailable;

	while ( total_cycle < wWait &&
	  !check_event_any(current_status = find_command_status(rReference), sStatus | event_no_success) &&
	  message_queue_status() && (!cCallback || (*cCallback)(rReference, sStatus) == 0) )
	 {
	nanosleep(&execute_cycle, NULL);
	total_cycle += cycle_elapse;
	 }

	//NOTE: this allows processing of a success response to a server termination request
	if (!message_queue_status() && !check_event_any(current_status, sStatus))
	{
	nanosleep(&execute_cycle, NULL);
	current_status = find_command_status(rReference);
	}

	if (check_event_any(current_status, sStatus)) return current_status;
	if (!message_queue_status())                  return event_no_connection;
	if (total_cycle >= wWait)                     return event_unavailable;
	return current_status;
}


command_event find_command_status(command_reference rReference)
/*obtain the status of a sent command*/
{ return retrieve_status(client_command_status, rReference); }


result clear_command_status(command_reference rReference)
{ return deregister_reference(client_command_status, rReference); }


text_info extract_remote_command(command_handle hHandle)
{
	if (!hHandle) return false;
	return extract_command(&queued_client_commands, hHandle);
}


multi_result send_stream_command(int fFile, command_handle hHandle)
{ return filtered_send_stream_command(fFile, hHandle, NULL, NULL); }


multi_result filtered_send_stream_command(remote_connection fFile, command_handle hHandle,
socket_reference sSocket, send_short_func sSend)
{
	if (!hHandle) return result_fail;

	command_transmit *copied_command = forward_command(&queued_client_commands, hHandle);
	if (!copied_command) return result_fail;

	common_output new_output(fFile);
	new_output.socket        = sSocket;
	new_output.output_sender = sSend;

    #ifdef PARAM_CACHE_COMMAND_OUTPUT
	bool outcome = copied_command->command_sendable() &&
	  export_data(copied_command, &new_output) && new_output.synchronize();
    #else
	bool outcome = copied_command->command_sendable() && export_data(copied_command, &new_output);
    #endif
	delete copied_command;

	if (outcome)                    return result_success;
	else if (new_output.is_closed()) return result_invalid;
	else                            return result_fail;
}


struct local_command_finder : public command_finder
{
	bool ATTR_INT new_command(command_transmit &bBase, const text_data &cCommand) const
	{ return empty_client_command(bBase, cCommand); }
};


command_handle insert_remote_command(text_info cCommand, text_info nName, text_info aAddress)
{
	if (!cCommand || !aAddress || !check_entity_label(aAddress)) return NULL;

	cstring_input local_input(cCommand);
	local_command_finder local_finder;
	command_transmit *new_command          = NULL;
	const command_transmit *queued_command = NULL;

	new_command = new command_transmit(&local_finder);
	if (!new_command) return NULL;

	if (!local_input.parse_command(new_command))
	{
	delete new_command;
	return NULL;
	}

	insert_remote_address(new_command->orig_address, aAddress);
	insert_remote_client(new_command->orig_address, new_command->orig_entity);
	new_command->orig_entity = nName? nName : entity_name();

	if (!next_address_scope(new_command->target_address))
	{
	delete new_command;
	return NULL;
	}

	if (!(queued_command = register_new_command(new_command)))
	{
	delete new_command;
	return NULL;
	}

	return (command_handle) queued_command;
}


static common_input internal_stream_input(-1);

multi_result receive_stream_command(command_handle *cCommand, int fFile,
text_info nName, text_info aAddress)
{
	//TODO: protect this with a mutex or annotate that it's not reentrant
	if (!aAddress || !check_entity_label(aAddress) || !cCommand) return result_fail;

	internal_stream_input.file_swap(fFile);
	local_command_finder local_finder;
	command_transmit *new_command          = NULL;
	const command_transmit *queued_command = NULL;

	new_command = new command_transmit(&local_finder);
	if (!new_command) return result_fail;

	if (!internal_stream_input.set_input_mode(input_binary | input_allow_underrun))
	{
	delete new_command;
	return result_fail;
	}

	internal_stream_input.reset_transmission_limit();

	if (!internal_stream_input.parse_command(new_command) || internal_stream_input.is_terminated())
	{
	delete new_command;
	if (internal_stream_input.is_terminated()) return result_invalid;
	return result_fail;
	}

	if (!new_command->command_ready())
	{
	delete new_command;
	*cCommand = NULL;
	return result_success;
	}

	if (!internal_stream_input.set_input_mode(input_binary)); //NOTE: to turn off underrun

	insert_remote_address(new_command->orig_address, aAddress);
	insert_remote_client(new_command->orig_address, new_command->orig_entity);
	new_command->orig_entity = nName? nName : entity_name();

	if (!next_address_scope(new_command->target_address))
	{
	delete new_command;
	return result_fail;
	}

	if (!(queued_command = register_new_command(new_command)))
	{
	delete new_command;
	return result_fail;
	}

	*cCommand = (command_handle) queued_command;
	return result_success;
}


result residual_stream_input()
{ return internal_stream_input.residual_data(); }


multi_result buffered_receive_stream_command(command_handle *cCommand, int fFile,
text_info nName, text_info aAddress, external_buffer *bBuffer)
{ return filtered_receive_stream_command(cCommand, fFile, nName, aAddress, bBuffer, 0, NULL); }


multi_result filtered_receive_stream_command(command_handle *cCommand, remote_connection fFile,
text_info nName, text_info aAddress, external_buffer *bBuffer, socket_reference sSocket,
receive_short_func rReceive)
{
	if (!aAddress || !check_entity_label(aAddress) || !bBuffer || !cCommand) return result_fail;

	if (!bBuffer->input_source)
	bBuffer->input_source = new buffered_common_input(fFile, bBuffer);
	else
	bBuffer->input_source->file_swap(fFile);

	bBuffer->input_source->socket         = sSocket;
	bBuffer->input_source->input_receiver = rReceive;
	local_command_finder local_finder;
	command_transmit *new_command          = NULL;
	const command_transmit *queued_command = NULL;

	new_command = new command_transmit(&local_finder);
	if (!new_command) return result_fail;

	if (!bBuffer->input_source->set_input_mode(input_binary | input_allow_underrun))
	{
	delete new_command;
	return result_fail;
	}

	if (!bBuffer->input_source->parse_command(new_command))
	{
	delete new_command;
	if (bBuffer->input_source->is_terminated()) return result_invalid;
	return result_fail;
	}

	if (!new_command->command_ready())
	{
	delete new_command;
	*cCommand = NULL;
	return result_success;
	}

	if (!bBuffer->input_source->set_input_mode(input_binary)); //NOTE: to turn off underrun

	insert_remote_address(new_command->orig_address, aAddress);
	insert_remote_client(new_command->orig_address, new_command->orig_entity);
	new_command->orig_entity = nName? nName : entity_name();

	if (!next_address_scope(new_command->target_address))
	{
	delete new_command;
	return result_fail;
	}

	if (!(queued_command = register_new_command(new_command)))
	{
	delete new_command;
	return result_fail;
	}

	*cCommand = (command_handle) queued_command;
	return result_success;
}


result buffered_residual_stream_input(external_buffer *bBuffer)
{
	if (!bBuffer) return false;
	return bBuffer->current_data.size() || bBuffer->current_line.size() ||
	  bBuffer->loaded_data.size() ||
	  (bBuffer->input_source && !bBuffer->input_source->empty_read());
}


//(defined for 'resource-client.h')
result set_alternate_sender(command_handle hHandle, text_info rResponses)
{
	//NOTE: only resource clients can change sender
	if (!check_permission_all(local_type(), type_resource_client)) return false;

	if (!hHandle || !check_entity_label(rResponses)) return false;
	return set_response_entity(&queued_client_commands, hHandle, rResponses);
}


result insert_remote_address(command_handle hHandle, text_info tTarget)
{
	if (!strlen(tTarget)) return true;
	if (!check_address_label(tTarget) || !check_next_to_client(tTarget)) return false;
	return insert_remote_scope(&queued_client_commands, hHandle, tTarget);
}


result insert_remote_target(command_handle hHandle, text_info cClient, text_info aAddress)
{
	if (!strlen(cClient) || !aAddress) return false;
	if (!check_entity_label(cClient) || !check_entity_label(aAddress)) return false;
	text_data temporary = aAddress;
	insert_remote_client(temporary, cClient);
	return insert_remote_scope(&queued_client_commands, hHandle, temporary.c_str());
}


//(defined for 'service-client.h')
result insert_service_address(command_handle hHandle, text_info sService)
{
	if (!strlen(sService)) return false;
	if (!check_entity_label(sService)) return false;
	text_data temporary;
	insert_remote_client(temporary, sService);
	return insert_remote_scope(&queued_client_commands, hHandle, temporary.c_str());
}


result set_target_to_server_of(command_handle hHandle, text_info cClient, text_info aAddress)
{
	if (!strlen(cClient)) return false;
	if (!check_entity_label(cClient)) return false;
	if (!check_address_label(aAddress) || !check_next_to_client(aAddress)) return false;
	return set_server_address(&queued_client_commands, hHandle, cClient, aAddress);
}


text_info get_next_address(command_handle hHandle, char *cCopy, unsigned int sSize)
{ return next_remote_address(&queued_client_commands, hHandle, cCopy, sSize); }


//(from 'plugin-dev/manual-command.hpp')
command_handle manual_command(text_info nName, external_command *cCommand)
{
	//NOTE: must have target name because plugins can't execute on servers!
	if (!nName)
	{
	delete cCommand;
	return NULL;
	}

	command_transmit *new_block            = NULL;
	const command_transmit *queued_command = NULL;
	new_block = new command_transmit(NULL);
	if (!new_block)
	{
	delete cCommand;
	return NULL;
	}

	if (!new_block->set_command(cCommand))
	{
	delete new_block;
	return NULL;
	}

	if (!lookup_command(new_block->command_name(), new_block->execute_type))
	{
	delete new_block;
	return NULL;
	}

	new_block->orig_entity   = entity_name();
	new_block->target_entity = nName;


	if (!new_block->command_ready() || !(queued_command = register_new_command(new_block)))
	{
	delete new_block;
	return NULL;
	}

	return (command_handle) queued_command;
}


//(from 'plugin-dev/manual-command.hpp')
command_handle manual_response(message_handle rRequest, struct external_command *cCommand)
{
	GENERAL_RESPONSE(rRequest, cCommand)
}
