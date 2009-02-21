/* This software is released under the BSD License.
 |
 | Copyright (c) 2009, Kevin P. Barry [the resourcerver project]
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

#include "proto-admin-client.hpp"

extern "C" {
#include "api/tools.h"
}

#include "api-source-macro.hpp"
#include "load-macro.hpp"
#include "protocol/constants.hpp"

extern "C" {
#include "command/api-command.h"
}


//proto_create_client command=================================================
RSERVR_AUTO_BUILTIN_TAG(create_client)
const text_data proto_create_client_system = "system";
const text_data proto_create_client_exec   = "exec";

	RSERVR_COMMAND_DEFAULT_CONSTRUCT(proto_create_client),
	create_type(0x00), user_id(0), group_id(0), min_priority(0),
	max_permissions(0), options(0x00) { }


	proto_create_client::proto_create_client(text_info cCommand, uid_t uUid,
	gid_t gGid, command_priority pPriority, permission_mask pPerm,
	create_flags fFlags, bool pProcess) :
	RSERVR_COMMAND_INIT_BASE(RSERVR_BUILTIN_TAG(create_client)),
	create_type(0x01), user_id(uUid), group_id(gGid), min_priority(pPriority),
	max_permissions(pPerm), options(fFlags)
	{
	RSERVR_COMMAND_CREATE_CHECK(create_client, type_admin_client, type_server)

	if (pProcess) set_property(this->command_properties, return_pid_label);

	RSERVR_TEMP_CONVERSION

	command_text.push_back(cCommand? cCommand : "");

	RSERVR_COMMAND_ADD_TEXT(proto_create_client_system)
	RSERVR_CONVERT16_ADD(user_id)
	RSERVR_CONVERT16_ADD(group_id)
	RSERVR_CONVERT16_ADD(min_priority)
	RSERVR_CONVERT16_ADD(max_permissions)
	RSERVR_CONVERT16_ADD(options)
	RSERVR_COMMAND_ADD_BINARY(cCommand? cCommand : "")
	}


	proto_create_client::proto_create_client(info_list cCommand, uid_t uUid,
	gid_t gGid, command_priority pPriority, permission_mask pPerm,
	create_flags fFlags, bool pProcess) :
	RSERVR_COMMAND_INIT_BASE(RSERVR_BUILTIN_TAG(create_client)),
	create_type(0x02), user_id(uUid), group_id(gGid), min_priority(pPriority),
	max_permissions(pPerm), options(fFlags)
	{
	RSERVR_COMMAND_CREATE_CHECK(create_client, type_admin_client, type_server)

	if (pProcess) set_property(this->command_properties, return_pid_label);

	RSERVR_TEMP_CONVERSION

	RSERVR_COMMAND_ADD_TEXT(proto_create_client_exec)
	RSERVR_CONVERT16_ADD(user_id)
	RSERVR_CONVERT16_ADD(group_id)
	RSERVR_CONVERT16_ADD(min_priority)
	RSERVR_CONVERT16_ADD(max_permissions)
	RSERVR_CONVERT16_ADD(options)

	storage_section *new_section = NULL;

	RSERVR_COMMAND_ADD_SECTION(new_section);

	if (new_section && cCommand) while (*cCommand)
	 {
	RSERVR_COMMAND_ADD_BINARY_TO_SECTION(new_section, *cCommand)
	command_text.push_back(*cCommand++);
	 }
	}


	RSERVR_SERVER_EVAL_HEAD(proto_create_client)
	{
	pid_t process = -1;

	if (create_type == 0x01)
	 {
	if (command_text.size() != 1) return RSERVR_EVAL_REJECTED;
	std::string temporary = command_text[0];

	if ((process = RSERVR_SERVER_ARG->create_system_client(&temporary[0], user_id, group_id,
	  min_priority, max_permissions, options)) < 0)
	return RSERVR_EVAL_REJECTED;
	 }

	else if (create_type == 0x02)
	 {
	if (command_text.size() < 1) return RSERVR_EVAL_REJECTED;
	std::vector <text_data> temporary1 = command_text;
	std::vector <char*> temporary2;
	temporary2.resize(temporary1.size() + 1);

	for (unsigned int I = 0; I < temporary2.size(); I++)
	if (I < temporary1.size()) temporary2[I] = &temporary1[I][0];
	else                       temporary2[I] = NULL;

	if ((process = RSERVR_SERVER_ARG->create_exec_client(&temporary2[0], user_id, group_id,
	  min_priority, max_permissions, options)) < 0)
	return RSERVR_EVAL_REJECTED;
	 }

	else return RSERVR_EVAL_REJECTED;

	if (!is_property_set(this->command_properties, return_pid_label))
	return RSERVR_EVAL_COMPLETE;

	char conversion[RSERVR_MAX_CONVERT_SIZE] = { 0x00 };
	convert_integer10(process, conversion);

	if (!external_command::auto_response(RSERVR_INFO_ARG, RSERVR_EVAL_COMPLETE, conversion))
	return RSERVR_EVAL_REJECTED;

	return RSERVR_EVAL_NONE;
	}


	RSERVR_COMMAND_PARSE_HEAD(proto_create_client)
	{
	RSERVR_COMMAND_INPUT_CHECK
	RSERVR_COMMAND_PARSE_CHECK(create_client, type_server, type_any_client)
	RSERVR_COMMAND_INPUT_SET

	RSERVR_CLEAR_COMMAND
	RSERVR_TEMP_STORAGE

	command_text.clear();

	RSERVR_EXTRACT_TEXT
	RSERVR_SET_AUTO_DELETE(type_deleter)

	if      (RSERVR_TEMP_STRING == proto_create_client_system) create_type = 0x01;
	else if (RSERVR_TEMP_STRING == proto_create_client_exec)   create_type = 0x02;
	else return NULL;

	RSERVR_UTILIZE(type_deleter)

	RSERVR_AUTO_ADD_TEXT
	RSERVR_PARSE16(user_id)

	RSERVR_AUTO_ADD_TEXT
	RSERVR_PARSE16(group_id)

	RSERVR_AUTO_ADD_TEXT
	RSERVR_PARSE16(min_priority)

	RSERVR_AUTO_ADD_TEXT
	RSERVR_PARSE16(max_permissions)

	RSERVR_AUTO_ADD_TEXT
	RSERVR_PARSE16(options)


	if (create_type == 0x01)
	 {
	RSERVR_AUTO_ADD_ANY
	command_text.push_back(RSERVR_TEMP_STRING);
	 }

	else if (create_type == 0x02) RSERVR_AUTO_ADD_LIST(command_text)

	else return NULL;


	RSERVR_PARSE_END
	}


RSERVR_SERVER_COMMAND_DEFAULTS(proto_create_client, RSERVR_BUILTIN_TAG(create_client), type_admin_client)

RSERVR_GENERATOR_DEFAULT( proto_create_client, \
  type_admin_client, type_server, \
  type_server,       type_any_client, \
  command_server | command_builtin | command_no_remote );
//END proto_create_client command===============================================


//proto_detached_client command=================================================
RSERVR_AUTO_BUILTIN_TAG(detached_client)

	RSERVR_COMMAND_DEFAULT_CONSTRUCT(proto_detached_client),
	min_priority(0), max_permissions(0), options(0x00) { }


	proto_detached_client::proto_detached_client(text_info sSocket,
	command_priority pPriority, permission_mask pPerm, create_flags fFlags) :
	RSERVR_COMMAND_INIT_BASE(RSERVR_BUILTIN_TAG(detached_client)),
	min_priority(pPriority), max_permissions(pPerm), options(fFlags),
	socket_name(sSocket? sSocket : "")
	{
	RSERVR_COMMAND_CREATE_CHECK(detached_client, type_admin_client | type_server_control, type_server)

	RSERVR_TEMP_CONVERSION

	RSERVR_CONVERT16_ADD(min_priority)
	RSERVR_CONVERT16_ADD(max_permissions)
	RSERVR_CONVERT16_ADD(options)
	RSERVR_COMMAND_ADD_BINARY(socket_name)
	}


	RSERVR_SERVER_EVAL_HEAD(proto_detached_client)
	{
	return RSERVR_SERVER_ARG->connect_detached_client(socket_name.c_str(), min_priority,
	  max_permissions, options)? RSERVR_EVAL_COMPLETE : RSERVR_EVAL_REJECTED;
	}


	RSERVR_COMMAND_PARSE_HEAD(proto_detached_client)
	{
	RSERVR_COMMAND_INPUT_CHECK
	RSERVR_COMMAND_PARSE_CHECK(detached_client, type_server, type_any_client)
	RSERVR_COMMAND_INPUT_SET

	RSERVR_CLEAR_COMMAND
	RSERVR_TEMP_STORAGE

	socket_name.clear();

	RSERVR_AUTO_ADD_TEXT
	RSERVR_PARSE16(min_priority)

	RSERVR_AUTO_ADD_TEXT
	RSERVR_PARSE16(max_permissions)

	RSERVR_AUTO_ADD_TEXT
	RSERVR_PARSE16(options)

	RSERVR_AUTO_COPY_ANY(socket_name)

	RSERVR_PARSE_END
	}


RSERVR_SERVER_COMMAND_DEFAULTS(proto_detached_client, RSERVR_BUILTIN_TAG(detached_client), type_admin_client)

RSERVR_GENERATOR_DEFAULT( proto_detached_client, \
  type_admin_client | type_server_control, type_server, \
  type_server,                             type_any_client, \
  command_server | command_builtin | command_no_remote );
//END proto_detached_client command=============================================


//proto_remove_client command===================================================
RSERVR_AUTO_BUILTIN_TAG(remove_client)
const text_data proto_remove_client_term = "term";
const text_data proto_remove_client_kill = "kill";

	RSERVR_COMMAND_DEFAULT_CONSTRUCT(proto_remove_client),
	remove_type(0x00) { }


	proto_remove_client::proto_remove_client(text_info nName, bool tType) :
	RSERVR_COMMAND_INIT_BASE(RSERVR_BUILTIN_TAG(remove_client)),
	remove_type(tType? 0x02 : 0x01), client_name(nName? nName : "")
	{
	if (remove_type == 0x01)
	RSERVR_COMMAND_CREATE_CHECK(remove_client, type_admin_client | type_server_control, type_server)

	else
	RSERVR_COMMAND_CREATE_CHECK(remove_client, type_admin_client, type_server)

	if (remove_type == 0x01)      RSERVR_COMMAND_ADD_TEXT(proto_remove_client_term)
	else if (remove_type == 0x02) RSERVR_COMMAND_ADD_TEXT(proto_remove_client_kill)

	RSERVR_COMMAND_ADD_TEXT(client_name)
	}


	RSERVR_SERVER_EVAL_HEAD(proto_remove_client)
	{
	if (remove_type == 0x01)
	 {
	return RSERVR_SERVER_ARG->terminate_client(client_name.c_str())?
	  RSERVR_EVAL_COMPLETE : RSERVR_EVAL_REJECTED;
	 }

	else if (remove_type == 0x02)
	 {
	return RSERVR_SERVER_ARG->kill_client(client_name.c_str())?
	  RSERVR_EVAL_COMPLETE : RSERVR_EVAL_REJECTED;
	 }

	else return RSERVR_EVAL_REJECTED;
	}


	RSERVR_COMMAND_PARSE_HEAD(proto_remove_client)
	{
	RSERVR_COMMAND_INPUT_CHECK
	RSERVR_COMMAND_PARSE_CHECK(remove_client, type_server, type_any_client)
	RSERVR_COMMAND_INPUT_SET

	RSERVR_CLEAR_COMMAND
	RSERVR_TEMP_STORAGE

	client_name.clear();

	RSERVR_EXTRACT_TEXT
	RSERVR_SET_AUTO_DELETE(type_deleter)

	if      (RSERVR_TEMP_STRING == proto_remove_client_term) remove_type = 0x01;
	else if (RSERVR_TEMP_STRING == proto_remove_client_kill) remove_type = 0x02;
	else return NULL;

	RSERVR_UTILIZE(type_deleter)

	RSERVR_AUTO_COPY_ANY(client_name)

	RSERVR_PARSE_END
	}
	//----------------------------------------------------------------------


RSERVR_SERVER_COMMAND_IMMEDIATE(proto_remove_client, RSERVR_BUILTIN_TAG(remove_client), type_admin_client)

RSERVR_GENERATOR_DEFAULT( proto_remove_client, \
  type_admin_client, type_server, \
  type_server,       type_any_client, \
  command_server | command_builtin | command_no_remote );
//END proto_remove_client command===============================================


//proto_remove_find_client command==============================================
RSERVR_AUTO_BUILTIN_TAG(remove_find_client)

	RSERVR_COMMAND_DEFAULT_CONSTRUCT(proto_remove_find_client),
	remove_type(0x00) { }


	proto_remove_find_client::proto_remove_find_client(text_info nName, bool tType) :
	RSERVR_COMMAND_INIT_BASE(RSERVR_BUILTIN_TAG(remove_find_client)),
	remove_type(tType? 0x02 : 0x01), client_expression(nName? nName : "")
	{
	if (remove_type == 0x01)
	RSERVR_COMMAND_CREATE_CHECK(remove_find_client, type_admin_client | type_server_control, type_server)

	else
	RSERVR_COMMAND_CREATE_CHECK(remove_find_client, type_admin_client, type_server)

	if (remove_type == 0x01)      RSERVR_COMMAND_ADD_TEXT(proto_remove_client_term)
	else if (remove_type == 0x02) RSERVR_COMMAND_ADD_TEXT(proto_remove_client_kill)

	RSERVR_COMMAND_ADD_BINARY(client_expression)
	}


	RSERVR_SERVER_EVAL_HEAD(proto_remove_find_client)
	{
	if (remove_type == 0x01)
	 {
	return RSERVR_SERVER_ARG->terminate_find_client(client_expression.c_str())?
	  RSERVR_EVAL_COMPLETE : RSERVR_EVAL_REJECTED;
	 }

	else if (remove_type == 0x02)
	 {
	return RSERVR_SERVER_ARG->kill_find_client(client_expression.c_str())?
	  RSERVR_EVAL_COMPLETE : RSERVR_EVAL_REJECTED;
	 }

	else return RSERVR_EVAL_REJECTED;
	}


	RSERVR_COMMAND_PARSE_HEAD(proto_remove_find_client)
	{
	RSERVR_COMMAND_INPUT_CHECK
	RSERVR_COMMAND_PARSE_CHECK(remove_find_client, type_server, type_any_client)
	RSERVR_COMMAND_INPUT_SET

	RSERVR_CLEAR_COMMAND
	RSERVR_TEMP_STORAGE

	client_expression.clear();

	RSERVR_EXTRACT_TEXT
	RSERVR_SET_AUTO_DELETE(type_deleter)

	if      (RSERVR_TEMP_STRING == proto_remove_client_term) remove_type = 0x01;
	else if (RSERVR_TEMP_STRING == proto_remove_client_kill) remove_type = 0x02;
	else return NULL;

	RSERVR_UTILIZE(type_deleter)

	RSERVR_AUTO_COPY_ANY(client_expression)

	RSERVR_PARSE_END
	}


RSERVR_SERVER_COMMAND_IMMEDIATE(proto_remove_find_client, RSERVR_BUILTIN_TAG(remove_find_client), type_admin_client)

RSERVR_GENERATOR_DEFAULT( proto_remove_find_client, \
  type_admin_client, type_server, \
  type_server,       type_any_client, \
  command_server | command_builtin | command_no_remote );
//END proto_remove_find_client command==========================================


//proto_remove_pid_client command===============================================
RSERVR_AUTO_BUILTIN_TAG(remove_pid_client)

	RSERVR_COMMAND_DEFAULT_CONSTRUCT(proto_remove_pid_client),
	remove_type(0x00), client_pid(-1) { }


	proto_remove_pid_client::proto_remove_pid_client(pid_t pPid, bool tType) :
	RSERVR_COMMAND_INIT_BASE(RSERVR_BUILTIN_TAG(remove_pid_client)),
	remove_type(tType? 0x02 : 0x01), client_pid(pPid)
	{
	if (remove_type == 0x01)
	RSERVR_COMMAND_CREATE_CHECK(remove_pid_client, type_admin_client | type_server_control, type_server)

	else
	RSERVR_COMMAND_CREATE_CHECK(remove_pid_client, type_admin_client, type_server)

	RSERVR_TEMP_CONVERSION

	if (remove_type == 0x01)      RSERVR_COMMAND_ADD_TEXT(proto_remove_client_term)
	else if (remove_type == 0x02) RSERVR_COMMAND_ADD_TEXT(proto_remove_client_kill)

	RSERVR_CONVERT10_ADD(client_pid)
	}


	RSERVR_SERVER_EVAL_HEAD(proto_remove_pid_client)
	{
	if (remove_type == 0x01)
	 {
	return RSERVR_SERVER_ARG->terminate_pid_client(client_pid)?
	  RSERVR_EVAL_COMPLETE : RSERVR_EVAL_REJECTED;
	 }

	else if (remove_type == 0x02)
	 {
	return RSERVR_SERVER_ARG->kill_pid_client(client_pid)?
	  RSERVR_EVAL_COMPLETE : RSERVR_EVAL_REJECTED;
	 }

	else return RSERVR_EVAL_REJECTED;
	}


	RSERVR_COMMAND_PARSE_HEAD(proto_remove_pid_client)
	{
	RSERVR_COMMAND_INPUT_CHECK
	RSERVR_COMMAND_PARSE_CHECK(remove_pid_client, type_server, type_any_client)
	RSERVR_COMMAND_INPUT_SET

	RSERVR_CLEAR_COMMAND
	RSERVR_TEMP_STORAGE

	RSERVR_EXTRACT_TEXT
	RSERVR_SET_AUTO_DELETE(type_deleter)

	if      (RSERVR_TEMP_STRING == proto_remove_client_term) remove_type = 0x01;
	else if (RSERVR_TEMP_STRING == proto_remove_client_kill) remove_type = 0x02;
	else return NULL;

	RSERVR_UTILIZE(type_deleter)

	RSERVR_AUTO_ADD_TEXT
	RSERVR_PARSE10(client_pid)

	RSERVR_PARSE_END
	}


RSERVR_SERVER_COMMAND_IMMEDIATE(proto_remove_pid_client, RSERVR_BUILTIN_TAG(remove_pid_client), type_admin_client)

RSERVR_GENERATOR_DEFAULT( proto_remove_pid_client, \
  type_admin_client, type_server, \
  type_server,       type_any_client, \
  command_server | command_builtin | command_no_remote );
//END proto_remove_pid_client command===========================================


//proto_terminate_server command================================================
RSERVR_AUTO_BUILTIN_TAG(terminate_server)

	RSERVR_COMMAND_DEFAULT_CONSTRUCT(proto_terminate_server) { }


	proto_terminate_server::proto_terminate_server() :
	RSERVR_COMMAND_INIT_BASE(RSERVR_BUILTIN_TAG(terminate_server))
	{
	RSERVR_COMMAND_CREATE_CHECK(terminate_server, type_admin_client | type_server_control, type_server)
	RSERVR_COMMAND_ADD_NULL
	}


	RSERVR_SERVER_EVAL_HEAD(proto_terminate_server)
	{ return RSERVR_SERVER_ARG->terminate_server()? RSERVR_EVAL_COMPLETE : RSERVR_EVAL_REJECTED; }


	RSERVR_COMMAND_PARSE_HEAD(proto_terminate_server)
	{
	RSERVR_COMMAND_INPUT_CHECK
	RSERVR_COMMAND_PARSE_CHECK(terminate_server, type_server, type_any_client)
	RSERVR_COMMAND_INPUT_SET

	RSERVR_CLEAR_COMMAND
	RSERVR_COMMAND_ADD_NULL

	RSERVR_PARSE_END
	}


RSERVR_SERVER_COMMAND_IMMEDIATE(proto_terminate_server, RSERVR_BUILTIN_TAG(terminate_server), type_admin_client)

RSERVR_GENERATOR_DEFAULT( proto_terminate_server, \
  type_admin_client | type_server_control, type_server, \
  type_server,        type_any_client, \
  command_server | command_builtin | command_no_remote );
//END proto_terminate_server command============================================


//proto_find_clients command====================================================
RSERVR_AUTO_BUILTIN_TAG(find_clients)

	RSERVR_COMMAND_DEFAULT_CONSTRUCT(proto_find_clients),
	required(0x00), denied(~0x00) { }


	proto_find_clients::proto_find_clients(text_info nName,
	permission_mask iInclude, permission_mask eExclude) :
	RSERVR_COMMAND_INIT_BASE(RSERVR_BUILTIN_TAG(find_clients)),
	name_expression(nName? nName : ""), required(iInclude), denied(eExclude)
	{
	RSERVR_COMMAND_CREATE_CHECK(find_clients, type_admin_client, type_server)

	RSERVR_TEMP_CONVERSION

	RSERVR_COMMAND_ADD_BINARY(name_expression)
	RSERVR_CONVERT16_ADD(required)
	RSERVR_CONVERT16_ADD(denied)
	}


	RSERVR_SERVER_EVAL_HEAD(proto_find_clients)
	{
	return RSERVR_SERVER_ARG->find_registered_clients(name_expression.c_str(), required, denied)?
	  RSERVR_EVAL_NONE : RSERVR_EVAL_REJECTED;
	}


	RSERVR_COMMAND_PARSE_HEAD(proto_find_clients)
	{
	RSERVR_COMMAND_INPUT_CHECK
	RSERVR_COMMAND_PARSE_CHECK(find_clients, type_admin_client, type_none)
	RSERVR_COMMAND_INPUT_SET

	RSERVR_CLEAR_COMMAND
	RSERVR_TEMP_STORAGE

	name_expression.clear();

	RSERVR_AUTO_COPY_ANY(name_expression)

	RSERVR_AUTO_ADD_TEXT
	RSERVR_PARSE16(required)

	RSERVR_AUTO_ADD_TEXT
	RSERVR_PARSE16(denied)

	RSERVR_PARSE_END
	}


RSERVR_SERVER_COMMAND_IMMEDIATE(proto_find_clients, RSERVR_BUILTIN_TAG(find_clients), type_admin_client)

RSERVR_GENERATOR_DEFAULT( proto_find_clients, \
  type_admin_client, type_server, \
  type_admin_client, type_none, \
  command_server | command_builtin );
//END proto_find_clients command================================================


DEFINE_LOAD_FUNCTIONS_START(admin)
ADD_COMMAND_GENERATOR(proto_create_client)
ADD_COMMAND_GENERATOR(proto_detached_client)
ADD_COMMAND_GENERATOR(proto_remove_client)
ADD_COMMAND_GENERATOR(proto_remove_find_client)
ADD_COMMAND_GENERATOR(proto_remove_pid_client)
ADD_COMMAND_GENERATOR(proto_terminate_server)
ADD_COMMAND_GENERATOR(proto_find_clients)
DEFINE_LOAD_FUNCTIONS_END
