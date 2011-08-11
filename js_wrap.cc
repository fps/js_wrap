
#include <iostream>
#include <string>
#include <cstdio>
#include <cstdlib>

#include <signal.h>
#include <unistd.h>

#include <jack/jack.h>
#include <jack/session.h>

#include <boost/program_options/cmdline.hpp>
#include <boost/program_options/environment_iterator.hpp>
#include <boost/program_options/eof_iterator.hpp>
#include <boost/program_options/errors.hpp>
#include <boost/program_options/option.hpp>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/positional_options.hpp>
#include <boost/program_options/value_semantic.hpp>
#include <boost/program_options/variables_map.hpp>
#include <boost/program_options/version.hpp>

#include <glib.h>
#include <sys/wait.h>
#include <signal.h>

bool quit = false;
bool got_jack_session_event = false;

jack_session_event_t *ev = 0;


extern "C"
{
	void signal_handler(int signal)
	{
		switch (signal)
		{
			case SIGTERM:
				quit = true;
			break;

			default:
			break;
		}
	}
	
	void session_callback(jack_session_event_t *event, void *p) {
		ev = event;
		got_jack_session_event = true;
	}
}

void print_usage ()
{
	std::cout << "Usage:" << std::endl;
	std::cout << "js_wrap [js_wrap options] -- [commandline to start app]" << std::endl;
 	std::cout << "See --help for commandline options" << std::endl;
}

namespace po = boost::program_options;
po::options_description desc("Allowed options");

jack_client_t *jack_client = 0;

int shutdown_timeout = 5;

std::string command_line;

void handle_jack_session_event() {
	//! Copy pasta slightly adapted from the jack session client walkthrough..
	jack_session_event_t *e = (jack_session_event_t *) ev;
	char command[10000];
	
	snprintf(command, sizeof(command), "js_wrap -U %s -- %s", e->client_uuid, command_line.c_str());
	
	//save_setup(filename);
	
	ev->command_line = strdup(command);
	jack_session_reply(jack_client, e);
	
	if (ev->type == JackSessionSaveAndQuit) {
		quit = true;
	}
	
	jack_session_event_free( ev );
}


int main (int argc, char *argv[])
{
	std::cout << "JS-Wrap - a small Jack-Session wrapper for non-JS apps." << std::endl;
	std::cout << "Copyright 2007 Florian Paul Schmidt (GPL v2)" << std::endl;

	
	signal(SIGTERM, signal_handler);
	signal(SIGCHLD, SIG_IGN);

	// We strip out all arguments after "--" and concenate them
	// to the app_commandline string
	int old_argc = argc;

	for (int i = 0; i < argc; ++i)
	{
		if (std::string (argv[i]) == "--")
		{
			argc = i;
			break;
		}
	}

	std::string uuid;

	desc.add_options()
		("help,h", "produce this help message")
		("version,v", "show version")
		("shutdown-timeout,s", po::value<int>(&shutdown_timeout), "Set the timeout time in seconds until js_wrap gives up waiting for the guest process to stop. First it is sent a SIGTERM, then the shutdown-timeout time is waited, then a SIGKILL is sent (default 5(s)).")
		("UUID,U", po::value<std::string>(&uuid), "jack session UUID")
		;
		
	po::variables_map vm;

	try
	{
		po::store(po::parse_command_line(argc, argv, desc), vm);
		po::notify(vm);
	}
	catch (boost::program_options::unknown_option u)
	{
		std::cout << "[JS-Wrap]: Error parsing options: Unknown option. See lash_wrap --help." << std::endl;
		exit (EXIT_FAILURE);
	}

	if (vm.count("help")) {
		print_usage ();
		std::cout << desc << "\n";
		exit (EXIT_SUCCESS);
	}

	if (vm.count("version")) {
		std::cout << "Version 0.1" << std::endl;
		exit (EXIT_SUCCESS);
	}
	
	jack_client = jack_client_open("js_wrap", JackSessionID, NULL, uuid.c_str());
	if (!jack_client) exit(EXIT_FAILURE);
	
	jack_set_session_callback(jack_client, ::session_callback, 0);
	jack_activate(jack_client);
	std::stringstream stream;
	for (int i = argc+1; i < old_argc; ++i) {
		stream << argv[i] << " ";
	}
	command_line = stream.str();
	
	//std::cout << "cmd: " << command_line << std::endl;
	// Ok, let's try to spawn the app
	GPid child_id;
	GError *error;
	if (!(g_spawn_async (NULL, argv + argc + 1, NULL, (GSpawnFlags)(G_SPAWN_SEARCH_PATH|G_SPAWN_DO_NOT_REAP_CHILD), 0, 0, &child_id, &error)))
	{
		std::cout << "[JS-Wrap]: Error: Failed to spawn process" << std::endl;

		if (jack_client)
			jack_client_close(jack_client);

		exit (EXIT_FAILURE);
	}
	while (!quit)
	{
		if (got_jack_session_event) {
			handle_jack_session_event();
		}
		usleep(10000);
		
		int status;
		pid_t ret;
		ret = waitpid (child_id, &status, WNOHANG);
		if (ret == child_id)
		{
			if (WIFEXITED(status) || WIFSIGNALED(status) || WCOREDUMP(status))
			{
				// process finished, so we leave too.
				// TODO: handle segfaults and other signals
				std::cout << "[Lash-Wrap]: Exiting, because the app exited. Bye.." << std::endl;

				// close connected clients before exiting
				if (jack_client)
					jack_client_close(jack_client);

				exit (EXIT_SUCCESS);
			}
		}
	}
	
	kill (child_id, SIGTERM);
	for (int i = 0; i < shutdown_timeout; ++i)
	{
		std::cout << "[JS-Wrap]: Waiting for child process... " << shutdown_timeout - i << std::endl;
		int status;
		int ret = waitpid (child_id, &status, WNOHANG);
		if (ret == child_id)
		{
			if (WIFEXITED(status) || WIFSIGNALED(status) || WCOREDUMP(status))
			{
				std::cout << "[JS-Wrap]: Child exited gracefully. Exiting..." << std::endl;
				exit(EXIT_SUCCESS);
			}
		}
		if (ret == -1)
		{
			std::cout << "[JS-Wrap]: waitpid() returned error code. Exiting..." << std::endl;
			exit(EXIT_FAILURE);
		}
		sleep(1);
	}

	// Ok, time to kill the app process as LASH told us to. send evil KILL signal
	std::cout << "[JS-Wrap]: Sending child process the KILL signal..." << std::endl;
	kill (child_id, SIGKILL);

	quit = false;

	while (!quit)
	{
		std::cout << "[JS-Wrap]: Waiting for child process..." << std::endl;
		int status;
		int ret = waitpid (child_id, &status, WNOHANG);

		// TODO better error handling
		if (ret == -1)
		{
			std::cout << "[JS-Wrap]: waitpid() returned error. Bye.." << std::endl;
			exit (EXIT_SUCCESS);
		}

		if (ret == child_id)
		{
			if (WIFEXITED(status) || WIFSIGNALED(status) ||WCOREDUMP(status))
			{
				// process finished, so we leave too.
				// TODO: handle segfaults and other signals
				std::cout << "[JS-Wrap]: Exiting, because the app exited (on signal SIGKILL). Bye.." << std::endl;
				exit (EXIT_SUCCESS);
			}
		}
		sleep (1);
	}
	
	jack_client_close(jack_client);
}