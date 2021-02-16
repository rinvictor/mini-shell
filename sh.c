#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <err.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <glob.h>

enum{
	Maxbuffer = 256,
	Maxargs = 256,
	Maxcoms = 64,
	Maxtokens= 64,
	Npipes = 256
};

typedef struct InfoCmd InfoCmd;
struct InfoCmd{
	char *cmd;
	char *args[Maxargs];
	
};


InfoCmd *cmds;
int ncommands;
char *filein;
char *fileout;
int bg = 0;

void printpromt(char *username, char *hostname){
	char curwdir[256];
	
	getcwd(curwdir, 256);
	if (strcmp(curwdir, getenv("HOME")) == 0)
		printf("%s@%s:~$ ", username, hostname);
	else 
		printf("%s@%s:%s$ ", username, hostname, curwdir);
}


int tokenize(char *str, char *delim,int max,char *tokensarray[]){
	int ntokens=0;
	char *token;

	
	token=strtok(str,delim);
	tokensarray[ntokens]=token;
	ntokens++;
	while (token != NULL){
		token = strtok(NULL, delim);
		if (token==NULL)
			break;
	
		tokensarray[ntokens]=token;
		ntokens++; 
		if (ntokens == max)
			break;
	}
	return ntokens;
}

char *delete_blanks(char *file){
	char *tokens[1];
	
	tokenize(file, " \n\t", 1, tokens);
	return tokens[0];
}

void split(char *s){
	
	char *tokens[Maxcoms];
	char *tokens2[Maxcoms];
	int i;
	int n;
	int k;
	char *p;
	
	n = tokenize(s,">",Maxcoms,tokens);
	if (n == 2){
		n = tokenize(tokens[1],"<",Maxcoms,tokens2);
		if(n == 2){
			fileout = tokens2[0];
			filein = tokens2[1];
		}
		else{
			fileout = tokens2[0];
		}
	}
	n = tokenize(s,"<",Maxcoms,tokens);
	if (n == 2){
		filein = tokens[1];
	}
	filein = delete_blanks(filein);
	fileout = delete_blanks(fileout);
	
	if ((filein!=NULL)&& (filein[0] == '$'))
		filein = getenv(filein +1);
	if ((fileout!=NULL) && (fileout[0] == '$'))
		fileout = getenv(fileout +1);

	ncommands=tokenize(s,"&|\n",Maxcoms,tokens);
	
	cmds=malloc(sizeof(InfoCmd)*ncommands);
	
	for(i=0;i<ncommands;i++){
		n=tokenize(tokens[i]," \t",Maxargs,cmds[i].args);
		for (k = 0; k < n; k++){ 
			if(cmds[i].args[k][0] == '$'){
				p = cmds[i].args[k] + 1;
				cmds[i].args[k] = getenv(&cmds[i].args[k][1]);
				
				if (cmds[i].args[k] == NULL)
					fprintf(stderr, "error: var %s does not exist", p);
			}
		}
		cmds[i].args[n]=NULL;
		
	}
}

char *getpath(char *cmd){
	char *path;
	char *tokens[Maxtokens];
	char cmdaux[Maxbuffer];
	char *cmdpath;
	int ntokens;
	int i;
	
	path=getenv("PATH");
	if (path==NULL)
		return NULL;
		
	if (access(cmd,X_OK)==0){
		cmdpath=strdup(cmd);
		return cmdpath;
	}	
	ntokens=tokenize(path,":",Maxtokens,tokens);
	for(i=0;i<ntokens;i++){
		snprintf(cmdaux,256,"%s/%s",tokens[i],cmd);
		if (access(cmdaux,X_OK)==0){
			cmdpath=strdup(cmdaux);
			return cmdpath;
		}
	}
	return NULL;
}

void close_pipes(int pipefd[Npipes][2]){
	int i;
	
	for (i = 0; i< ncommands-1; i++){
		close(pipefd[i][0]);
		close(pipefd[i][1]);
	}
}

int exists_pid(int *pids,int p, int n){
	int i;
	
	for(i=0;i<n;i++){
		if  (pids[i]==p)
			return 1;
		
	}
	
	return 0;
}

void executecmd(void){
	int i, j;
	pid_t *pid;
	pid_t pide;
	int pipefd[Npipes][2]; //--1 read, 0 read.
	int fd;
	pid=malloc(ncommands*sizeof(pid_t));
	int n = 0;
	glob_t globbuf;

	if (ncommands>1){
		for(i=0;i<ncommands-1;i++){
			if (pipe(pipefd[i] )== -1) {
				fprintf(stderr, "Error: pipe failed.\n");
				exit(EXIT_FAILURE);
			}

		}
	}
	
	for(i=0; i < ncommands; i++){
		pid[i]=fork();
		switch(pid[i]){
			case -1:
				err(EXIT_FAILURE,"fork");
			case 0:
			
				
				
				if (ncommands > 1){
					if (i == 0)
						dup2(pipefd[0][1], 1);
					else if (i == (ncommands - 1))
						dup2(pipefd[i-1][0], 0);
					else{
						dup2(pipefd[i-1][0], 0);
						dup2(pipefd[i][1], 1);
					}
					close_pipes(pipefd);
				}
				
			
				//first cmd input redirection
				if((i == 0) && (filein != NULL)){
					fd = open(filein, O_RDONLY);
					if(fd < 0)
						err(EXIT_FAILURE, "open");
					dup2(fd,0);
					close(fd);
				}
				//last cmd output redirection
				if((i == ncommands - 1) && (fileout != NULL)){
					fd = creat(fileout, 0644);
					if(fd < 0)
						err(EXIT_FAILURE, "open");
					dup2(fd,1);
					close(fd);
				}
				
				if ((bg) &&(i==0) && (filein==NULL)){
					
					fd=open("/dev/null",O_RDONLY);
					if (fd<0)
						err(EXIT_FAILURE,"open");
						
					dup2(fd,0);
					close(fd);
					
				}
				
			glob(cmds[i].args[0], GLOB_NOCHECK, NULL, &globbuf);
				j=1;
				while(cmds[i].args[j] != NULL){
					glob(cmds[i].args[j], GLOB_NOCHECK|GLOB_APPEND, NULL, &globbuf);
					j++;
				}
				
				cmds[i].cmd = getpath(globbuf.gl_pathv[0]);
				if (cmds[i].cmd==NULL){
					fprintf(stderr,"%s: command not found\n",globbuf.gl_pathv[0]);
				}
			
				execv(cmds[i].cmd, globbuf.gl_pathv);
				err(EXIT_FAILURE, "exec");
				
		}
	}
	close_pipes(pipefd);
	
	if (!bg){
			do{
				pide=wait(NULL);
				if (pide<0)
					err(EXIT_FAILURE,"wait");
				if (exists_pid(pid,pide,ncommands))
					n++;
				
			}while(n<ncommands);
	}
	free(pid);
}

void executecd(){
	char *home;
	
	home = getenv("HOME");

	if(cmds[0].args[1] == NULL){

		if (home == NULL)
			fprintf(stderr, "error cd\n");
		
		if (chdir(home) < 0)
			fprintf(stderr, "error cd\n");
	}
	else{
		if (chdir(cmds[0].args[1]) < 0)
			fprintf(stderr, "cd: %s: No such file or directory\n", cmds[0].args[1]);
	}
}

int setvar(char *buffer){
	
	int n;
	char* tokens[2];
	
	n=tokenize(buffer,"=\n",2,tokens);
	if (n==2){
		setenv(tokens[0],tokens[1],1);
		
	}
	return n;
}

int detect_bg(char *buffer){
	return((strchr(buffer, '&')) != NULL);
}

void welcomemessage (char* username){
	printf("\n--WELCOME %s!--\n\n", username);
}

int main(int argc, char *argv[]){
	
	char buffer[Maxbuffer];
	char username[256];
	char hostname[256];
	char *a;

	getlogin_r(username, Maxbuffer);
	gethostname(hostname, Maxbuffer);
	welcomemessage(username);
	do{	
		
		
		printpromt(username, hostname);
		filein = NULL;
		fileout = NULL;

		if (fgets(buffer, Maxbuffer, stdin) == NULL)
			break;
		
		if (buffer[0]=='\n')
			continue;
		if(setvar(buffer) == 2)
			continue;
		
		if(detect_bg(buffer)){
			bg =1;
			a=strchr(buffer,'&');
			*a='\0';
		}
		else
			bg=0;
			
		split(buffer);

		if (strcmp(cmds[0].args[0], "cd") == 0){
			executecd();
		}else{
			executecmd();
		}
		free(cmds);
		
	}while(1);
	exit(EXIT_SUCCESS);
	
}
