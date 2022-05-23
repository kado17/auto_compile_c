#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/inotify.h>

#define INOTIFY_EVENT_SIZE  (sizeof(struct inotify_event))
#define BUF_LEN (1024 * (INOTIFY_EVENT_SIZE + 16))
#define ARRAY_LEN 256

/*拡張子が.cのファイルパスを受け取り、拡張子を取り除いた文字列を返す
*filepath：対象のファイルパス
*result：結果の代入先
*/
void cut_extension_c(const char *filepath, char *result)
{
    strcpy(result, filepath);
    result[strlen(result)-2] = '\0';
}

/*受け取ったパスがディレクトリかを判別する
*path：対象のパス
*戻り値：ディレクトリならば1, それ以外ならば0
*/
int is_dirpath(const char *path)
{
    struct stat stat_buf;
    stat(path, &stat_buf);
    return S_ISDIR(stat_buf.st_mode);
}

/*日付と現在時刻の文字列を返す
*result：結果の代入先
*/
void get_date_and_time(char *result)
{
    char tmp[64];
    time_t t = time(NULL);
    strftime(tmp, sizeof(tmp), "%Y/%m/%d %a %H:%M:%S", localtime(&t));
    strcpy(result, tmp);
}

/*配列の先頭をデキューして、新たに値を一つエンキューする
* arr：対象の配列
*item：エンキューする値
*len：配列の長さ
*/
void queue_push(uint32_t *arr, uint32_t item, int len)
{   
    if (len < 2){
        arr[0] = item;
        return;
    }

    for (int i=1; i < len; i++){
        arr[i-1] = arr[i];
    }
    arr[len-1] = item;
}


/*ファイルの変化によって対応した処理を行う
*fd：ファイルディスクリプタ
*target_dirpath：対象のディレクトリパス
*/
void inotify_read_events(int fd, const char *target_dirpath)
{
    int i = 0, length = 0;
    char *p, buffer[BUF_LEN], date[64], filepath[ARRAY_LEN];
    static uint32_t event_hist[3];
    uint32_t cond1[]={IN_OPEN, IN_MODIFY, IN_CLOSE_WRITE}, cond2[]={IN_OPEN , IN_MODIFY, IN_CLOSE_NOWRITE};

    if((length = read(fd, buffer, BUF_LEN)) < 0){
        return;
    }

    while (i < length) {
        struct inotify_event *event = (struct inotify_event *) &buffer[i];
        snprintf(filepath, sizeof filepath, "%s/%s", target_dirpath, event->name);
        p = strrchr(filepath, '.');
        /*拡張子がcか確認*/
        if (p != NULL){
            if(strcmp(p, ".c")==0 && !is_dirpath(filepath)){

                if (event->mask & IN_CREATE){
                    get_date_and_time(date);
                    printf("[%s] %s が作成されました。\n", date, filepath);
                }

                queue_push(event_hist, event->mask, sizeof(event_hist)/sizeof(u_int32_t));
                /*イベント履歴が特定の並びの場合、コンパイルと実行を行う。*/
                if (memcmp(event_hist, cond1, sizeof(event_hist)) == 0 || (memcmp(event_hist, cond2, sizeof(event_hist)) == 0)){
                    memset(event_hist, 0,  sizeof(event_hist));
                                        char command[ARRAY_LEN*2+30], filepath_no_extension[ARRAY_LEN];
                    cut_extension_c(filepath, filepath_no_extension);                        
                    snprintf(command, sizeof command,"gcc -Wall \"%s\" -lm -o \"%s\"",filepath, filepath_no_extension);
                    get_date_and_time(date);
                    printf("[%s] %s をコンパイルします。\n", date, filepath);
                    system(command);

                    get_date_and_time(date);
                    printf("[%s] %s を実行します。\n", date, filepath);
                    printf("<----実行開始---->\n");
                    system(filepath_no_extension);
                    printf("<----実行終了---->\n");
                }
            }
        }
        i += INOTIFY_EVENT_SIZE + event->len;
    }
}

/*コマンドライン引数がなければ対象ディレクトリをカレントディレクトリに、
*コマンドライン引数が一つあればその引数のディレクトリを対象に処理を行う
*/
int main( int argc, char **argv ) 
{
    int fd, wd;
    char* target_dirpath = "./";

    if(2 < argc) {
        fprintf(stderr, "引数が多すぎます。\n");
        return 1;
    }else if(argc == 2){
        if(is_dirpath(argv[1])){
            target_dirpath = argv[1];
        }
        else{
            fprintf(stderr, "有効なディレクトリパスではありません。\n");
            return 1;
        } 
    }
    printf("対象ディレクトリ: %s\n終了は ctrl + c\n", target_dirpath);

    fd = inotify_init();
    if(fd == -1) {
        perror("inotify_init");
        return 1;
    }
    
    wd = inotify_add_watch(fd, target_dirpath, 
            IN_MODIFY | IN_OPEN | IN_CLOSE_WRITE | IN_CLOSE_NOWRITE | IN_CREATE | IN_ACCESS | IN_ATTRIB);

    while(1) {
        inotify_read_events(fd, target_dirpath);
    }

    inotify_rm_watch(fd, wd);
    close(fd);

    return 0;
}