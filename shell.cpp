/**
	* Shell
	* Operating Systems
	* v20.08.28
	*/

/**
	Hint: Control-click on a functionname to go to the definition
	Hint: Ctrl-space to auto complete functions and variables
	*/

// function/class definitions you are going to use
#include <iostream>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sys/param.h>
#include <signal.h>
#include <string.h>
#include <fcntl.h>

#include <vector>

// although it is good habit, you don't have to type 'std' before many objects by including this line
using namespace std;

struct Command {
	vector<string> parts = {};
};

struct Expression {
	vector<Command> commands;
	string inputFromFile = "";
	string outputToFile = "";
	bool background = false;
};

// Parses a string to form a vector of arguments. The seperator is a space char (' ').
vector<string> splitString(const string& str, char delimiter = ' ') {
	vector<string> retval;
	for (size_t pos = 0; pos < str.length(); ) {
		// look for the next space
		size_t found = str.find(delimiter, pos);
		// if no space was found, this is the last word
		if (found == string::npos) {
			retval.push_back(str.substr(pos));
			break;
		}
		// filter out consequetive spaces
		if (found != pos)
			retval.push_back(str.substr(pos, found-pos));
		pos = found+1;
	}
	return retval;
}

// wrapper around the C execvp so it can be called with C++ strings (easier to work with)
// always start with the command itself
// always terminate with a NULL pointer
// DO NOT CHANGE THIS FUNCTION UNDER ANY CIRCUMSTANCE
int execvp(const vector<string>& args) {
	// build argument list
	const char** c_args = new const char*[args.size()+1];
	for (size_t i = 0; i < args.size(); ++i) {
		c_args[i] = args[i].c_str();
	}
	c_args[args.size()] = nullptr;
	// replace current process with new process as specified
	::execvp(c_args[0], const_cast<char**>(c_args));
	// if we got this far, there must be an error
	int retval = errno;
	// in case of failure, clean up memory
	delete[] c_args;
	return retval;
}

// Executes a command with arguments. In case of failure, returns error code.
int executeCommand(const Command& cmd) {
	auto& parts = cmd.parts;
	if (parts.size() == 0)
		return EINVAL;

	// execute external commands
	int retval = execvp(parts);
	return retval;
}

void displayPrompt() {
	char buffer[512];
	char* dir = getcwd(buffer, sizeof(buffer));
	if (dir) {
		cout << "\e[32m" << dir << "\e[39m"; // the strings starting with '\e' are escape codes, that the terminal application interpets in this case as "set color to green"/"set color to default"
	}
	cout << "$ ";
	flush(cout);
}

string requestCommandLine(bool showPrompt) {
	if (showPrompt) {
		displayPrompt();
	}
	string retval;
	getline(cin, retval);
	return retval;
}

// note: For such a simple shell, there is little need for a full blown parser (as in an LL or LR capable parser).
// Here, the user input can be parsed using the following approach.
// First, divide the input into the distinct commands (as they can be chained, separated by `|`).
// Next, these commands are parsed separately. The first command is checked for the `<` operator, and the last command for the `>` operator.
Expression parseCommandLine(string commandLine) {
	Expression expression;
	vector<string> commands = splitString(commandLine, '|');
	for (size_t i = 0; i < commands.size(); ++i) {
		string& line = commands[i];
		vector<string> args = splitString(line, ' ');
		if (i == commands.size() - 1 && args.size() > 1 && args[args.size()-1] == "&") {
			expression.background = true;
			args.resize(args.size()-1);
		}
		if (i == commands.size() - 1 && args.size() > 2 && args[args.size()-2] == ">") {
			expression.outputToFile = args[args.size()-1];
			args.resize(args.size()-2);
		}
		if (i == 0 && args.size() > 2 && args[args.size()-2] == "<") {
			expression.inputFromFile = args[args.size()-1];
			args.resize(args.size()-2);
		}
		expression.commands.push_back({args});
	}
	return expression;
}

int forkExecuteCommand(Expression& expression) {
	int retval = 0;
	pid_t child = fork();
	if (child < 0){
		cerr << "Fork Failed" << endl;
		exit(1);
	}
	if (child == 0) {
		if (expression.inputFromFile != "") {
			int fdesc = open(expression.inputFromFile.c_str(), O_RDONLY);
			dup2(fdesc, STDIN_FILENO);
			close(fdesc);
		}

		if (expression.outputToFile != "") {
			int fdesc = open(expression.outputToFile.c_str(), O_WRONLY | O_CREAT, 0644);
			dup2(fdesc, STDOUT_FILENO);
			close(fdesc);
		}
		int retval = executeCommand(expression.commands[0]);
		cout << "Invalid command" << endl;
		abort();
	}
	if (!expression.background) waitpid(child, &retval, 0);
	return retval;
}

int forkExecuteCommands(Expression& expression) {
	int retval = 0;

	int channels[expression.commands.size() - 1][2];
	// Create all pipes
	for (int i = 0; i < expression.commands.size() - 1; i++) {
		if (pipe(channels[i]) != 0) {
			cerr << "Failed to create pipe\n";
		}
	}

	pid_t children[expression.commands.size()];

	// Connect all commands with pipes
	for (int i = 0; i < expression.commands.size(); i++) {
		children[i] = fork();
		if (children[i] == 0) {
			if (i == 0) {
				dup2(channels[i][1], STDOUT_FILENO);
				if (expression.inputFromFile != "") {
					int fdesc = open(expression.inputFromFile.c_str(), O_RDONLY);
					dup2(fdesc, STDIN_FILENO);
					close(fdesc);
				}
			} else if (i == expression.commands.size() - 1) {
				dup2(channels[i-1][0], STDIN_FILENO);
				if (expression.outputToFile != "") {
					int fdesc = open(expression.outputToFile.c_str(), O_WRONLY | O_CREAT, 0644);
					dup2(fdesc, STDOUT_FILENO);
					close(fdesc);
				}
			} else {
				dup2(channels[i][1], STDOUT_FILENO);
				dup2(channels[i-1][0], STDIN_FILENO);
			}

			for (int i = 0; i < expression.commands.size() - 1; i++) {
				close(channels[i][0]);
				close(channels[i][1]);
			}

			executeCommand(expression.commands[i]);
			cout << "Invalid command" << endl;
			abort();
		}
	}

	for (int i = 0; i < expression.commands.size() - 1; i++) {
		close(channels[i][0]);
		close(channels[i][1]);
	}

	if (!expression.background) {
		for (int i = 0; i < expression.commands.size(); i++) {
			if (i == expression.commands.size() - 1)
				waitpid(children[i], &retval, 0);
			else
				waitpid(children[i], nullptr, 0);
		}
	}
	return retval;
}

int executeExpression(Expression& expression) {
	// Check for empty expressionget
	if (expression.commands.size() == 0)
		return EINVAL;
	 
	if (expression.commands[0].parts[0] == "exit"){
		cout << "Exiting the shell" << endl;
		exit(1);
		return 1;
	}

	if (expression.commands[0].parts[0] == "cd"){
		chdir(expression.commands[0].parts[1].c_str());
		return 0;
	}

	int retval = 0;
	if (expression.commands.size() == 1) {
		retval = forkExecuteCommand(expression);
	} else {
		retval = forkExecuteCommands(expression);
	}

	return retval;
}

int normal(bool showPrompt) {
	while (cin.good()) {
		string commandLine = requestCommandLine(showPrompt);
		Expression expression = parseCommandLine(commandLine);
		int rc = executeExpression(expression);
		if (rc != 0)
			cerr << strerror(rc) << endl;
	}
	return 0;
}

int shell(bool showPrompt) {
	return normal(showPrompt);
}
