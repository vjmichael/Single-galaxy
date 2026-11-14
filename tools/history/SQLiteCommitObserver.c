/* S6-H4 Linux-only observation hook. Unchanged original native source/test
 * emits sqlite3_exec(COMMIT); preserve its actually committed SQLite via
 * SQLite online backup. No modification of the original game database. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <sqlite3.h>
#include <ctype.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

static __thread int observing=0;
static unsigned char captured[256];
static int (*real_exec)(sqlite3*,const char*,int(*)(void*,int,char**,char**),void*,char**)=NULL;
static int commit_sql(const char* sql){
    if(!sql)return 0;
    while(isspace((unsigned char)*sql))++sql;
    const char expected[]="COMMIT";
    for(int i=0;i<6;++i)
        if(!sql[i] || toupper((unsigned char)sql[i])!=expected[i])return 0;
    sql+=6;
    while(isspace((unsigned char)*sql))++sql;
    if(*sql==';')++sql;
    while(isspace((unsigned char)*sql))++sql;
    return *sql=='\0';
}
static int allowed(int version){
    const char *limits=getenv("SG_H4_CAPTURE_VERSIONS");
    if(!limits || !*limits)return version>=86&&version<=135;
    const char *p=limits;
    while(*p){
        char *end=NULL;
        const long first=strtol(p,&end,10);
        if(end==p||first<1||first>255)return 0;
        long last=first;
        if(*end=='-'){
            p=end+1;last=strtol(p,&end,10);
            if(end==p||last<first||last>255)return 0;
        }
        if(version>=first&&version<=last)return 1;
        if(*end=='\0')break;
        if(*end!=',')return 0;
        p=end+1;
    }
    return 0;
}
static int actual_version(sqlite3* db){
    sqlite3_stmt *q=NULL;
    if(sqlite3_prepare_v2(db,"SELECT state FROM snapshots ORDER BY generation DESC LIMIT 1",-1,&q,NULL)!=SQLITE_OK){
        if(q)sqlite3_finalize(q);
        return 0;
    }
    int version=0;
    if(sqlite3_step(q)==SQLITE_ROW){
        const unsigned char *raw=sqlite3_column_blob(q,0);
        const int size=sqlite3_column_bytes(q,0);
        const char prefix[]="galaxy-kernel-v";
        if(raw && size>19 && memcmp(raw,prefix,sizeof(prefix)-1)==0){
            int n=0,i=(int)sizeof(prefix)-1;
            while(i<size&&i<(int)sizeof(prefix)+3&&raw[i]>='0'&&raw[i]<='9'){
                n=n*10+(raw[i++]-'0');
            }
            if(i<size && raw[i]=='|'&&n>=1&&n<=255)version=n;
        }
    }
    sqlite3_finalize(q);
    return version;
}
static int commands(sqlite3* db){
    sqlite3_stmt *q=NULL;
    if(sqlite3_prepare_v2(db,"SELECT COUNT(*) FROM operation_log WHERE kind=1",-1,&q,NULL)!=SQLITE_OK){
        if(q)sqlite3_finalize(q);
        return 0;
    }
    const int n=sqlite3_step(q)==SQLITE_ROW?sqlite3_column_int(q,0):0;
    sqlite3_finalize(q);
    return n;
}
static int accepted_receipts(sqlite3* db){
    sqlite3_stmt *q=NULL;
    if(sqlite3_prepare_v2(db,"SELECT COUNT(*) FROM receipts",-1,&q,NULL)!=SQLITE_OK){
        if(q)sqlite3_finalize(q);
        return 0;
    }
    const int n=sqlite3_step(q)==SQLITE_ROW?sqlite3_column_int(q,0):0;
    sqlite3_finalize(q);
    return n;
}
static void observe(sqlite3* src){
    const char *root=getenv("SG_H4_CAPTURE_DIR");
    if(!root||!*root||strlen(root)>PATH_MAX-100)return;
    const int version=actual_version(src);
    if(version<1||version>255||captured[version]||!allowed(version)||
       commands(src)<1||accepted_receipts(src)<1)return;
    captured[version]=1;
    char path[PATH_MAX],temporary[PATH_MAX];
    if(snprintf(path,sizeof(path),"%s/v%03d-pid%ld.sqlite",root,version,(long)getpid())>=(int)sizeof(path))return;
    if(snprintf(temporary,sizeof(temporary),"%s.incomplete",path)>=(int)sizeof(temporary))return;
    int fd=open(temporary,O_CREAT|O_EXCL|O_WRONLY|O_NOFOLLOW,0600);
    if(fd<0)return;
    close(fd);
    sqlite3* output=NULL;
    if(sqlite3_open_v2(temporary,&output,SQLITE_OPEN_READWRITE,NULL)!=SQLITE_OK){
        if(output)sqlite3_close(output);
        unlink(temporary);
        return;
    }
    sqlite3_backup* backup=sqlite3_backup_init(output,"main",src,"main");
    const int stepped=backup?sqlite3_backup_step(backup,-1):SQLITE_ERROR;
    const int finished=backup?sqlite3_backup_finish(backup):SQLITE_ERROR;
    const int closed=sqlite3_close(output);
    if(stepped==SQLITE_DONE&&finished==SQLITE_OK&&closed==SQLITE_OK&&rename(temporary,path)==0){
        fprintf(stderr,"SG_H4_NATIVE_CAPTURE|version=%d|pid=%ld|path=%s\n",version,(long)getpid(),path);
        return;
    }
    unlink(temporary);
}
int sqlite3_exec(sqlite3* db,const char* sql,
                 int(*callback)(void*,int,char**,char**),void* arg,char** error){
    if(!real_exec){
        void *address=dlsym(RTLD_NEXT,"sqlite3_exec");
        memcpy(&real_exec,&address,sizeof(real_exec));
        if(!real_exec)return SQLITE_MISUSE;
    }
    const int result=real_exec(db,sql,callback,arg,error);
    if(result==SQLITE_OK && db && commit_sql(sql) && !observing){
        observing=1;observe(db);observing=0;
    }
    return result;
}
