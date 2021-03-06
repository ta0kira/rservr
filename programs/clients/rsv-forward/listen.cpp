/* This software is released under the BSD License.
 |
 | Copyright (c) 2013, Kevin P. Barry [the resourcerver project]
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

#include "listen.hpp"

extern "C" {
#include "param.h"
#include "api/client-timing.h"
#include "api/message-queue.h"
#include "api/label-check.h"
#include "plugins/rsvp-netcntl-hook.h"
}

#include <map>

#ifdef RSV_NET
extern "C" {
#include "api/tools.h"
}
#endif

#include <stdio.h> //'fprintf', 'fopen', 'remove'
#include <string.h> //'strerror', 'strcmp', 'strtok', 'strdup', 'strlen'
#include <errno.h> //'errno'
#include <pthread.h> //pthreads
#include <sys/socket.h> //'socket', 'setsockopt'
#include <fcntl.h> //'fcntl'
#include <sys/stat.h> //'stat'
#include <time.h> //'nanosleep'
#include <unistd.h> //'close'
#include <stddef.h> //'offsetof'

#ifdef RSV_NET
#include <netinet/in.h> //socket macros
#include <arpa/inet.h> //'inet_ntoa'
#endif

#ifdef RSV_LOCAL
#include <sys/un.h> //socket macros
#endif

#include "external/clist.hpp"
#include "external/global-sentry-pthread.hpp"

#include "global/regex-check.hpp"
#include "global/condition-block.hpp"

#include "connect.hpp"

extern "C" {
#include "messages.h"
#include "socket-table.h"
#include "remote-filter.h"
#include "select-breaker.h"
#include "security-filter.h"
#include "socket-check.h"
}


//separators--------------------------------------------------------------------

#ifdef RSV_NET
static const char serial_separator[] = PARAM_NET_SERIAL_SEP;
static const char port_separator[]   = PARAM_NET_PORT_SEP;
#endif
#ifdef RSV_LOCAL
static const char serial_separator[] = PARAM_LOCAL_SERIAL_SEP;
#endif

//END separators----------------------------------------------------------------


//regex filtering---------------------------------------------------------------

//TODO: add logging points for screening failure

static data::clist <regex_check> listen_allow, listen_require;

bool add_listen_allow(const char *rRegex)
{
	if (!rRegex) return false;
	regex_check new_regex;
	if (!(new_regex = rRegex)) return false;
	return listen_allow.add_element(new_regex);
}

bool add_listen_require(const char *rRegex)
{
	if (!rRegex) return false;
	regex_check new_regex;
	if (!(new_regex = rRegex)) return false;
	return listen_require.add_element(new_regex);
}


static bool invert_result(const regex_check &lLeft, const char *rRight)
{ return !(lLeft == rRight); }


static bool split_address(const char*, uint32_t&, uint16_t&);

static bool screen_listen(const char *aAddress)
{
	if (!aAddress) return false;

    #if defined(PARAM_FORCE_SOCKET_AUTHORIZE) && defined(RSV_NET)
	if (!listen_allow.size()) return false;
	bool allow = false;
    #else
	if (!listen_allow.size() && !listen_require.size()) return true;
	bool allow = !listen_allow.size();
    #endif

#ifdef RSV_NET
	in_addr binary_address;
	uint16_t binary_port;
	if (!split_address(aAddress, binary_address.s_addr, binary_port)) return false;

	char binary_text[RSERVR_MAX_CONVERT_SIZE];
	std::string revised_address = inet_ntoa(binary_address);
	std::string revised_port    = convert_integer10(ntohs(binary_port), binary_text);

	std::string address = revised_address + port_separator + revised_port;
#endif

#ifdef RSV_LOCAL
	std::string address = aAddress;
#endif

	allow |= listen_allow.find(address.c_str()) != data::not_found;
	if (listen_require.f_find(address.c_str(), &invert_result) != data::not_found) return false;

	return allow;
}

//END regex filtering-----------------------------------------------------------


//listen data and control-------------------------------------------------------

static unsigned int individual_connection_max = 0;
static unsigned int total_connection_max      = 0;
static unsigned int total_listen_max          = 0;


#ifdef RSV_NET

static std::string listen_address = "any";
static uint32_t listen_address_binary = (uint32_t) -1;

static text_info listen_address_text()
{ return listen_address.c_str(); }

static uint32_t get_listen_address()
{
	if (listen_address_binary == (uint32_t) -1)
	listen_address_binary = try_get_address(listen_address_text());
	return listen_address_binary;
}

bool set_listen_address(const char *aAddress)
{
	if (!aAddress) return false;
	listen_address = aAddress;
	listen_address_binary = (uint32_t) -1;
	return get_listen_address() != (uint32_t) -1;
}

#endif


void set_total_listen_max(unsigned int mMax)
{ total_listen_max = mMax; }

void set_connection_max(unsigned int mMax)
{ individual_connection_max = mMax; }

void set_total_connection_max(unsigned int mMax)
{ total_connection_max = mMax; }


struct listen_info
{
	inline ATTR_INL listen_info() :
	allow_passthru(passthru_status()), limit(individual_connection_max),
	connections(0) { }

	inline ATTR_INL listen_info(const std::string &lLocation) :
	location(lLocation), allow_passthru(passthru_status()),
	limit(individual_connection_max), connections(0) { }

	std::string location;
	bool         allow_passthru;
	unsigned int limit;
	unsigned int connections;
};

typedef std::map <int, listen_info> listen_list;

struct listen_data
{
	inline ATTR_INL listen_data() :
	total_connections(0) { }

	listen_list sockets;
	unsigned int total_connections;
};

typedef protect::capsule <listen_data> protected_listen_data;

static protect::literal_capsule <listen_data, global_sentry_pthread <> > internal_listen_data;

//END listen data and control---------------------------------------------------


//access modules----------------------------------------------------------------

bool ATTR_INT add_new_listen(int sSocket, const std::string &lLocation)
{
	protected_listen_data::write_object object = internal_listen_data.writable();
	if (!object) return false;

	if (total_listen_max && object->sockets.size() >= total_listen_max)
	return false;

	if ( object->sockets.find(sSocket) == object->sockets.end() &&
	     object->sockets.size() + 1 >= FD_SETSIZE)
	//(add one to account for the select breaker)
	return false;

	object->sockets[sSocket] = listen_info(lLocation);
	return true;
}


bool ATTR_INT add_connection_precheck(int sSocket)
{
	protected_listen_data::read_object object = internal_listen_data.readable();
	if (!object) return false;

	if ( total_connection_max &&
	     object->total_connections >= total_connection_max )
	return false;

	listen_list::const_iterator position = object->sockets.find(sSocket);
	if (position != object->sockets.end())
	if ( position->second.limit &&
	     position->second.connections >= position->second.limit )
	return false;

	return true;
}


static bool remove_common(listen_list &lList, const listen_list::iterator &pPosition)
{
	if (pPosition == lList.end()) return false;

#ifdef RSV_LOCAL
	remove(pPosition->second.location.c_str());
#endif

	close(pPosition->first);
	lList.erase(pPosition);

	return true;
}


bool ATTR_INT remove_listen(const std::string &lLocation)
{
	protected_listen_data::write_object object = internal_listen_data.writable();
	if (!object) return false;

	listen_list::iterator position = object->sockets.begin(),
	                      end      = object->sockets.end();

	while (position != end)
	 {
	if (position->second.location == lLocation) break;
	++position;
	 }

	return remove_common(object->sockets, position);
}


bool ATTR_INT remove_socket_fd(int sSocket)
{
	protected_listen_data::write_object object = internal_listen_data.writable();
	if (!object) return false;

	listen_list::iterator position = object->sockets.find(sSocket);

	return remove_common(object->sockets, position);
}


bool ATTR_INT remove_all_sockets()
{
	protected_listen_data::write_object object = internal_listen_data.writable();
	if (!object) return false;

	while (remove_common(object->sockets, object->sockets.begin()));
	return true;
}


bool ATTR_INT add_connection_count(int sSocket)
{
	protected_listen_data::write_object object = internal_listen_data.writable();
	if (!object) return false;

	if ( total_connection_max &&
	     object->total_connections >= total_connection_max )
	return false;

	listen_list::iterator position = object->sockets.find(sSocket);
	if (position != object->sockets.end())
	//NOTE: this is for listen-based connections; not for initiated connections
	 {
	if ( object->sockets[sSocket].limit &&
	     object->sockets[sSocket].connections >=
	     object->sockets[sSocket].limit )
	return false;

	++object->sockets[sSocket].connections;
	 }

	++object->total_connections;
	return true;
}


bool ATTR_INT remove_connection_count(int sSocket)
{
	protected_listen_data::write_object object = internal_listen_data.writable();
	if (!object) return false;

	listen_list::iterator position = object->sockets.find(sSocket);
	if (position != object->sockets.end())
	//NOTE: this is for listen-based connections; not for initiated connections
	if (object->sockets[sSocket].connections > 0)
	--object->sockets[sSocket].connections;

	if (object->total_connections > 0)
	--object->total_connections;

	return true;
}


bool ATTR_INT get_listen_select_list(fd_set *lList)
{
	protected_listen_data::read_object object = internal_listen_data.readable();
	if (!object) return false;

	//NOTE: empty is a failure so that it doesn't actually enter 'select'
	if (!object->sockets.size() || object->sockets.size() >= FD_SETSIZE)
	return false;

	FD_ZERO(lList);

	listen_list::const_iterator current = object->sockets.begin(),
	                            end     = object->sockets.end();

	while (current != end)
	 {
	FD_SET(current->first, lList);
	++current;
	 }

	return true;
}


bool ATTR_INT get_passthru_flag(int sSocket)
{
	protected_listen_data::read_object object = internal_listen_data.readable();
	if (!object) return false;

	listen_list::const_iterator position = object->sockets.find(sSocket);
	return position != object->sockets.end() && position->second.allow_passthru;
}


bool ATTR_INT get_set_socket_list(fd_set *lList, data::clist <const int> &tTable)
{
	protected_listen_data::read_object object = internal_listen_data.readable();
	if (!object) return false;

	tTable.reset_list();

	listen_list::const_iterator current = object->sockets.begin(),
	                            end     = object->sockets.end();

	while (current != end)
	{
	if (FD_ISSET(current->first, lList))
	tTable.add_element(current->first);
	++current;
	}

	return true;
}


bool ATTR_INT check_listen_location(const std::string &lLocation)
{
	protected_listen_data::read_object object = internal_listen_data.readable();
	if (!object) return false;

	listen_list::const_iterator current = object->sockets.begin(),
	                            end     = object->sockets.end();

	while (current != end)
	if ((current++)->second.location == lLocation) return false;

	return true;
}


bool ATTR_INT have_listen_sockets()
{
	protected_listen_data::read_object object = internal_listen_data.readable();
	return object && object->sockets.size();
}


bool ATTR_INT have_listen_room()
{
	if (!total_listen_max) return true;
	protected_listen_data::read_object object = internal_listen_data.readable();
	return object && object->sockets.size() < total_listen_max;
}


bool ATTR_INT copy_listen_locations(char ***tTable)
{
	protected_listen_data::read_object object = internal_listen_data.readable();
	if (!object) return false;

	char **new_list = new char*[ object->sockets.size() + 1 ];

	listen_list::const_iterator current = object->sockets.begin(),
	                            end     = object->sockets.end();

	unsigned int I = 0;

	while (current != end)
	new_list[I++] = strdup((current++)->second.location.c_str());
	new_list[I] = NULL;

	*tTable = new_list;

	return true;
};

//END access modules------------------------------------------------------------


//access module interface-------------------------------------------------------

#ifdef RSV_NET
static bool add_bound_socket(int sSocket, const std::string &aAddress, const std::string &pPort)
{ return add_new_listen(sSocket, aAddress + port_separator + pPort); }
#endif
#ifdef RSV_LOCAL
static bool add_bound_socket(int sSocket, const std::string &lLocation)
{ return add_new_listen(sSocket, lLocation); }
#endif

static bool pre_connection_check(int sSocket)
{ return add_connection_precheck(sSocket); }

bool pre_initiation_check()
{ return add_connection_precheck(-1); }

#ifdef RSV_NET
static bool remove_listen_socket(const std::string &aAddress, const std::string &pPort)
{ return remove_listen(aAddress + port_separator + pPort); }
#endif
#ifdef RSV_LOCAL
static bool remove_listen_socket(const std::string &lLocation)
{ return remove_listen(lLocation); }
#endif

static bool remove_bad_socket(int sSocket)
{ return remove_socket_fd(sSocket); }

static bool clear_all_sockets()
{ return remove_all_sockets(); }

bool add_listen_connection(int sSocket)
{ return add_connection_count(sSocket); }

void remove_listen_connection(int sSocket)
{ remove_connection_count(sSocket); }

static bool fill_listen_select(fd_set *lList)
{ return get_listen_select_list(lList); }

static bool get_set_sockets(fd_set *lList, data::clist <const int> &tTable)
{ return get_set_socket_list(lList, tTable); }

#ifdef RSV_NET
static bool check_unique_location(const std::string &aAddress, const std::string &pPort)
{ return check_listen_location(aAddress + port_separator + pPort); }
#endif
#ifdef RSV_LOCAL
static bool check_unique_location(const std::string &lLocation)
{ return check_listen_location(lLocation); }
#endif

static bool check_listen_sockets()
{ return have_listen_sockets(); }

static bool check_listen_room()
{ return have_listen_room(); }

static bool copy_listen_table(char ***tTable)
{ return copy_listen_locations(tTable); }

//END access module interface---------------------------------------------------


//socket management functions---------------------------------------------------

#ifdef RSV_NET

static uint16_t convert_port_server(const char *pPort)
{
	int value = 0x00;
	bool outcome = parse_integer10(pPort, &value) && value <= 0xffff && value > 0;
	return outcome? htons(value) : 0x00;
}


static bool split_address(const char *lLocation, uint32_t &aAddress, uint16_t &pPort)
{
	pPort = 0;
	aAddress = get_listen_address();
	in_addr temp_address;
	temp_address.s_addr = aAddress;

	std::string working_copy = lLocation, actual_port, actual_address;

	int port_position = working_copy.rfind(port_separator);
	if (port_position >= 0 && port_position < (signed) working_copy.size())
	{
	actual_port = working_copy.substr(port_position + 1,
	  working_copy.size() - (port_position + 1));

	working_copy.erase(port_position, working_copy.size() - port_position);
	pPort = convert_port_server(actual_port.c_str());

	temp_address.s_addr = aAddress = try_get_address(working_copy.c_str());
	}

	else pPort = convert_port_server(lLocation);

	actual_address = inet_ntoa(temp_address);

	return pPort && aAddress != (uint32_t) -1;
}


static int create_socket(const char *pPort, std::string &rRevisedAddress, std::string &rRevisedPort)
{
	in_addr binary_address;
	uint16_t binary_port;

	if (!pPort || !split_address(pPort, binary_address.s_addr, binary_port))
	{
    log_message_listen_deny(listen_address_text(), pPort);
	return -1;
	}

	char binary_text[RSERVR_MAX_CONVERT_SIZE];
	rRevisedAddress = inet_ntoa(binary_address);
	rRevisedPort = convert_integer10(ntohs(binary_port), binary_text);

	if (!check_unique_location(rRevisedAddress, rRevisedPort))
	{
    log_message_listen_deny(rRevisedAddress.c_str(), rRevisedPort.c_str());
	return -1;
	}

	//create socket

	struct sockaddr_in new_address;

	int new_socket = socket(PF_INET, SOCK_STREAM, 0);
	if (new_socket < 0)
	{
    log_message_listen_deny(rRevisedAddress.c_str(), rRevisedPort.c_str());
	return -1;
	}

	int current_state = fcntl(new_socket, F_GETFD);
	fcntl(new_socket, F_SETFD, current_state | FD_CLOEXEC);

	//bind socket

	new_address.sin_family = AF_INET;
	new_address.sin_port        = binary_port; //NOTE: don't call 'htons' here
	new_address.sin_addr.s_addr = binary_address.s_addr;

    #if defined(SOL_SOCKET) && defined(SO_REUSEADDR)
	int value = 1;
	setsockopt(new_socket, SOL_SOCKET, SO_REUSEADDR, (void*) &value,
	  sizeof value);
    #endif

	if (bind(new_socket, (struct sockaddr*) &new_address, sizeof new_address) < 0)
	{
    log_message_listen_deny(rRevisedAddress.c_str(), rRevisedPort.c_str());
	close(new_socket);
	return -1;
	}


	current_state = fcntl(new_socket, F_GETFL);
	fcntl(new_socket, F_SETFL, current_state | O_NONBLOCK);

	return new_socket;
}


static bool remove_server_socket(const char *pPort)
{
	in_addr binary_address;
	uint16_t binary_port;

	if (!pPort || !split_address(pPort, binary_address.s_addr, binary_port))
	{
    log_message_unlisten_deny(listen_address_text(), pPort);
	return -1;
	}

	char binary_text[RSERVR_MAX_CONVERT_SIZE];
	std::string revised_address, revised_port;
	revised_address = inet_ntoa(binary_address);
	revised_port = convert_integer10(ntohs(binary_port), binary_text);

	if (!remove_listen_socket(revised_address, revised_port))
	{
    log_message_unlisten_deny(revised_address.c_str(), revised_port.c_str());
	return false;
	}

    log_message_unlisten(revised_address.c_str(), revised_port.c_str());

	return true;
}

#endif


#ifdef RSV_LOCAL

static int create_socket(const char *nName, std::string &rRevised)
{
	if (!nName || !check_entity_label(nName)) return -1;

	rRevised = nName;

	if (!check_unique_location(rRevised))
	{
    log_message_listen_deny(nName);
	return -1;
	}

	//create socket

	struct sockaddr_un new_address;
	size_t new_length = 0;

	int new_socket = socket(PF_LOCAL, SOCK_STREAM, 0);
	if (new_socket < 0)
	{
    log_message_listen_deny(nName);
	return -1;
	}

	int current_state = fcntl(new_socket, F_GETFD);
	fcntl(new_socket, F_SETFD, current_state | FD_CLOEXEC);

	//bind socket

	new_address.sun_family = AF_LOCAL;
	strncpy(new_address.sun_path, nName, sizeof new_address.sun_path);

	new_length = (offsetof(struct sockaddr_un, sun_path) + SUN_LEN(&new_address) + 1);

	if (bind(new_socket, (struct sockaddr*) &new_address, new_length) < 0)
	{
    log_message_listen_deny(nName);
	close(new_socket);
	return -1;
	}

	//set socket permissions

	//TODO: add selectable group-accessible mode here
	mode_t socket_mode = S_IRUSR | S_IWUSR;
	if (chmod(nName, socket_mode) < 0)
	{
    log_message_listen_deny(nName);
	close(new_socket);
	return -1;
	}


	current_state = fcntl(new_socket, F_GETFL);
	fcntl(new_socket, F_SETFL, current_state | O_NONBLOCK);

	return new_socket;
}


static bool remove_server_socket(const char *nName)
{
	if (!nName)
	{
    log_message_unlisten_deny(nName);
	return false;
	}

	if (!remove_listen_socket(nName))
	{
    log_message_unlisten_deny(nName);
	return false;
	}

    log_message_unlisten(nName);

	return true;
}

#endif

//END socket management functions-----------------------------------------------


//listen thread control---------------------------------------------------------

static pthread_t select_thread = pthread_t();

static auto_mutex     connection_hold_mutex;
static auto_condition connection_hold_cond;

pthread_t listen_select_thread()
{ return select_thread; }


void unblock_connection_wait()
{
	send_select_break();
	connection_hold_cond.unblock();
}

static void *select_thread_loop(void*);

static inline bool select_thread_status()
{ return !pthread_equal(select_thread, pthread_t()); }


int start_select_thread()
{
	//NOTE: must return 'true' if empty; that isn't an error!
	if (!check_listen_sockets()) return true;
	if (select_thread_status() || !connection_hold_cond.activate()) return false;
	int outcome = pthread_create(&select_thread, NULL, &select_thread_loop, NULL);
	if (outcome < 0)
	{
	connection_hold_cond.deactivate();
	select_thread = pthread_t();
	}
	return true;
}


int stop_select_thread()
{
	pthread_t thread_copy = select_thread;
	if (!select_thread_status()) return 0;
	select_thread = pthread_t();
	pthread_cancel(thread_copy);
	connection_hold_cond.deactivate();
	return pthread_detach(thread_copy);
}


bool block_for_connect()
{
	return connection_hold_cond.active() && connection_hold_mutex.valid() &&
	       connection_hold_cond.block(connection_hold_mutex);
}

//END listen thread control-----------------------------------------------------


//configuration and mid-end functions-------------------------------------------

bool add_listen_socket(const char *lLocation)
{
#if defined(PARAM_ABSOLUTE_LOCAL_SOCKETS) && defined(RSV_LOCAL)
	if (strlen(lLocation) < 1 || lLocation[0] != '/')
	{
    log_message_listen_deny(lLocation);
	fprintf(stderr, "%s: can't create socket '%s': name must start with '/'\n", client_name, lLocation);
	return false;
	}
#endif

	int new_socket = -1;
#ifdef RSV_NET
	std::string revised_address, revised_port;
#endif
#ifdef RSV_LOCAL
	std::string revised_address;
#endif

	if ( !check_listen_room() ||
#ifdef RSV_NET
	     (new_socket = create_socket(lLocation, revised_address, revised_port)) < 0 ||
#endif
#ifdef RSV_LOCAL
	     (new_socket = create_socket(lLocation, revised_address)) < 0 ||
#endif
	     listen(new_socket, PARAM_FORWARD_MAX_WAITING) < 0 ||
#ifdef RSV_NET
	     !add_bound_socket(new_socket, revised_address, revised_port) )
#endif
#ifdef RSV_LOCAL
	     !add_bound_socket(new_socket, revised_address) )
#endif
	{
	if (new_socket >= 0)
	 {
#ifdef RSV_NET
    log_message_listen_deny(revised_address.c_str(), revised_port.c_str());
#endif
#ifdef RSV_LOCAL
    log_message_listen_deny(revised_address.c_str());
	remove(lLocation);
#endif
	close(new_socket);
	 }

#ifdef RSV_NET
	fprintf(stderr, "%s: can't bind socket to port '%s'\n", client_name, lLocation);
#endif
#ifdef RSV_LOCAL
	fprintf(stderr, "%s: can't create socket '%s'\n", client_name, lLocation);
#endif
	return false;
	}

	//NOTE: this is logged here because of multiple possible failures
#ifdef RSV_NET
    log_message_listen(revised_address.c_str(), revised_port.c_str());
#endif
#ifdef RSV_LOCAL
    log_message_listen(revised_address.c_str());
#endif

	unblock_connection_wait();
	return true;
}


void remove_listen_sockets()
{
	clear_all_sockets();
	connection_hold_cond.deactivate();
}

//END configuration and mid-end functions---------------------------------------


//select thread-----------------------------------------------------------------

static void *select_thread_loop(void *iIgnore)
{
	if (pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL) != 0)
	{
	pthread_detach(pthread_self());
	select_thread = pthread_t();
	return NULL;
	}

	if (!connection_hold_cond.active())
	{
	pthread_detach(pthread_self());
	select_thread = pthread_t();
	return NULL;
	}

	fd_set current_sockets;
	data::clist <const int> set_sockets;
	socklen_t new_length = 0;
	int new_connection   = -1;

#ifdef RSV_NET
	struct sockaddr_in new_address;
#endif

#ifdef RSV_LOCAL
	struct sockaddr_un new_address;
#endif


	if (!check_listen_sockets())
	{
	connection_hold_cond.deactivate();
	pthread_detach(pthread_self());
	select_thread = pthread_t();
	return NULL;
	}


	while (message_queue_status() && fill_listen_select(&current_sockets))
	{
	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL) != 0)
	 {
	connection_hold_cond.deactivate();
	pthread_detach(pthread_self());
	select_thread = pthread_t();
	return NULL;
	 }

	if (select(FD_SETSIZE, &current_sockets, NULL, NULL, NULL) >= 0 || errno == EBADF)
	//NOTE: don't check for 'EINTR'; that's how 'select' is broken
	 {
	pthread_testcancel(); //in case 'select' gets by when canceling
	if (pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL) != 0)
	  {
	connection_hold_cond.deactivate();
	pthread_detach(pthread_self());
	select_thread = pthread_t();
	return NULL;
	  }

	if (!get_set_sockets(&current_sockets, set_sockets)) continue;

	for (int I = 0; I < (signed) set_sockets.size(); I++)
	//signed allows decrementing 'I' for bad socket
	  {
	//TODO: add logging point to denote that a limit was reached
	if (!pre_connection_check(set_sockets[I]))
	   {
	shutdown( accept(set_sockets[I], (struct sockaddr*) &new_address, &new_length), SHUT_RDWR );
	continue;
	   }

	new_length = sizeof new_address;

	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL) != 0)
	   {
	connection_hold_cond.deactivate();
	pthread_detach(pthread_self());
	select_thread = pthread_t();
	return NULL;
	   }

	if ((new_connection = accept(set_sockets[I], (struct sockaddr*) &new_address, &new_length)) >= 0)
	   {
	if (pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL) != 0)
	    {
	connection_hold_cond.deactivate();
	pthread_detach(pthread_self());
	select_thread = pthread_t();
	return NULL;
	    }

	int current_state = fcntl(new_connection, F_GETFL);
	fcntl(new_connection, F_SETFL, current_state | O_NONBLOCK);


	int filter_result  = 0;
	int connect_result = 0;

	socket_reference new_reference = get_new_reference();

	if ( (filter_result = filter_incoming_address((socket_reference) set_sockets[I],
	        new_reference, (struct sockaddr*) &new_address, new_length)) < 0 ||
	     (connect_result = connect_from_host(new_reference, new_connection,
	        (struct sockaddr*) &new_address, new_length)) < 0 )
	//filter address and run connection filter
	    {
#ifdef RSV_NET
	if (connect_result < 0)
    log_message_incoming_fail(inet_ntoa(new_address.sin_addr));
#endif
	//NOTE: this is a "penalty" timeout to slow down recurrent invalid connection attempts
	//NOTE: this is only used on the listening end because a hostile connection attempt is likely NOT from this program
	//NOTE: this is used for local sockets, too, because there's no good reason not to
	struct timespec retry_penalty = local_default_slow_cycle();
	nanosleep(&retry_penalty, NULL);

	shutdown(new_connection, SHUT_RDWR);
	continue;
	    }

#ifdef RSV_NET
	if ( !add_listen_connection(new_connection, new_reference, set_sockets[I],
	         inet_ntoa(new_address.sin_addr), get_passthru_flag(set_sockets[I])) )
	shutdown(new_connection, SHUT_RDWR);
#endif
#ifdef RSV_LOCAL
	if ( !add_listen_connection(new_connection, new_reference, set_sockets[I],
	         new_address.sun_path, get_passthru_flag(set_sockets[I])) )
	shutdown(new_connection, SHUT_RDWR);
#endif
	   }

	else
	//if 'accept' failed...
	   {
	int error_copy = errno;
	if (pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL) != 0)
	    {
	connection_hold_cond.deactivate();
	pthread_detach(pthread_self());
	select_thread = pthread_t();
	return NULL;
	    }

	if (error_copy != EINTR && error_copy != EAGAIN && error_copy != ECONNABORTED)
	    {
	remove_bad_socket(set_sockets[I--]);
	continue;
	    }
	   }
	  }
	 }

	pthread_testcancel(); //in case 'select' gets by when canceling
	if (pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL) != 0)
	 {
	connection_hold_cond.deactivate();
	pthread_detach(pthread_self());
	select_thread = pthread_t();
	return NULL;
	 }
	}

	connection_hold_cond.deactivate();
	pthread_detach(pthread_self());
	select_thread = pthread_t();
	return NULL;
}

//END select thread-------------------------------------------------------------


//plug-in hooks-----------------------------------------------------------------

#ifdef RSV_NET
command_event __rsvp_netcntl_hook_net_listen_list(const struct netcntl_source_info *sSource, char ***lList)
#endif
#ifdef RSV_LOCAL
command_event __rsvp_netcntl_hook_local_listen_list(const struct netcntl_source_info *sSource, char ***lList)
#endif
{
	if (!sSource) return event_rejected;

    log_message_incoming_listen_list(sSource);

    #ifdef PARAM_SELF_TRUSTING_FORWARDER
	if (!trusted_remote_check(sSource)) return event_rejected;
    #endif

	if (!lList || *lList) return event_error;

	return copy_listen_table(lList)? event_complete : event_error;
}


#ifdef RSV_NET
command_event __rsvp_netcntl_hook_net_listen(const struct netcntl_source_info *sSource, text_info lLocation)
#endif
#ifdef RSV_LOCAL
command_event __rsvp_netcntl_hook_local_listen(const struct netcntl_source_info *sSource, text_info lLocation)
#endif
{
	if (!sSource) return event_rejected;

#if defined(PARAM_ABSOLUTE_LOCAL_SOCKETS) && defined(RSV_LOCAL)
	if (strlen(lLocation) < 1 || lLocation[0] != '/')
	{
    log_message_listen_deny(lLocation);
	return event_rejected;
	}
#endif

    log_message_incoming_listen(sSource, lLocation);
	if (!screen_listen(lLocation))
	{
#ifdef RSV_NET
    log_message_listen_deny(listen_address_text(), lLocation);
#endif
#ifdef RSV_LOCAL
    log_message_listen_deny(lLocation);
#endif
	return event_rejected;
	}

#ifdef PARAM_SELF_TRUSTING_FORWARDER
	if (!trusted_remote_check(sSource))
	{
    #ifdef RSV_NET
    log_message_listen_deny(listen_address_text(), lLocation);
    #endif
    #ifdef RSV_LOCAL
    log_message_listen_deny(lLocation);
    #endif
	return event_rejected;
	}
#endif

	int new_socket = -1;

#ifdef RSV_NET
	std::string revised_address, revised_port;
#endif
#ifdef RSV_LOCAL
	std::string revised_address;
#endif

	if ( !check_listen_room() ||
#ifdef RSV_NET
	     (new_socket = create_socket(lLocation, revised_address, revised_port)) < 0 ||
#endif
#ifdef RSV_LOCAL
	     (new_socket = create_socket(lLocation, revised_address)) < 0 ||
#endif
	     listen(new_socket, PARAM_FORWARD_MAX_WAITING) < 0 ||
#ifdef RSV_NET
	     !add_bound_socket(new_socket, revised_address, revised_port) )
#endif
#ifdef RSV_LOCAL
	     !add_bound_socket(new_socket, revised_address) )
#endif
	{
#ifdef RSV_NET
    log_message_listen_deny(revised_address.c_str(), revised_port.c_str());
#endif
#ifdef RSV_LOCAL
    log_message_listen_deny(lLocation);
#endif
	if (new_socket >= 0)
	 {
#ifdef RSV_LOCAL
	remove(lLocation);
#endif
	close(new_socket);
	 }

	return event_error;
	}
	else if (!select_thread_status() && !start_select_thread())
	{
#ifdef RSV_NET
    log_message_listen_deny(revised_address.c_str(), revised_port.c_str());
#endif
#ifdef RSV_LOCAL
    log_message_listen_deny(revised_address.c_str());
	remove(lLocation);
#endif
	remove_bad_socket(new_socket);

	return event_error;
	}

	//NOTE: this is logged here because of multiple possible failures
#ifdef RSV_NET
    log_message_listen(revised_address.c_str(), revised_port.c_str());
#endif
#ifdef RSV_LOCAL
    log_message_listen(revised_address.c_str());
#endif

	unblock_connection_wait();
	return event_complete;
}


#ifdef RSV_NET
command_event __rsvp_netcntl_hook_net_unlisten(const struct netcntl_source_info *sSource, text_info lLocation)
#endif
#ifdef RSV_LOCAL
command_event __rsvp_netcntl_hook_local_unlisten(const struct netcntl_source_info *sSource, text_info lLocation)
#endif
{
	if (!sSource) return event_rejected;

#if defined(PARAM_ABSOLUTE_LOCAL_SOCKETS) && defined(RSV_LOCAL)
	if (strlen(lLocation) < 1 || lLocation[0] != '/')
	{
    log_message_unlisten_deny(lLocation);
	return event_rejected;
	}
#endif

    log_message_incoming_unlisten(sSource, lLocation);

#ifdef PARAM_SELF_TRUSTING_FORWARDER
	if (!trusted_remote_check(sSource))
	{
    #ifdef RSV_NET
    log_message_unlisten_deny(listen_address_text(), lLocation);
    #endif
    #ifdef RSV_LOCAL
    log_message_unlisten_deny(lLocation);
    #endif
	return event_rejected;
	}
#endif

	return remove_server_socket(lLocation)? event_complete : event_error;
}

//END plug-in hooks-------------------------------------------------------------
