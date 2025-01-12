﻿#include <stdlib.h>
#include <memory.h>
#include <dirent.h>
#include <set>
#include "Util/CMD.h"
#include "Util/util.h"
#include "Util/logger.h"
#include "Util/File.h"
#include "Util/uv_errno.h"

using namespace std;
using namespace toolkit;

class CMD_main : public CMD {
public:
    CMD_main() {
        _parser.reset(new OptionParser(nullptr));


        (*_parser) << Option('r',/*该选项简称，如果是\x00则说明无简称*/
                             "rm",/*该选项全称,每个选项必须有全称；不得为null或空字符串*/
                             Option::ArgNone,/*该选项后面必须跟值*/
                             nullptr,/*该选项默认值*/
                             false,/*该选项是否必须赋值，如果没有默认值且为ArgRequired时用户必须提供该参数否则将抛异常*/
                             "是否删除或添加bom,默认添加bom头",/*该选项说明文字*/
                             nullptr);

        (*_parser) << Option('f',/*该选项简称，如果是\x00则说明无简称*/
                             "filter",/*该选项全称,每个选项必须有全称；不得为null或空字符串*/
                             Option::ArgRequired,/*该选项后面必须跟值*/
                             "c,cpp,cxx,c,h,hpp",/*该选项默认值*/
                             true,/*该选项是否必须赋值，如果没有默认值且为ArgRequired时用户必须提供该参数否则将抛异常*/
                             "文件后缀过滤器",/*该选项说明文字*/
                             nullptr);

        (*_parser) << Option('i',/*该选项简称，如果是\x00则说明无简称*/
                             "in",/*该选项全称,每个选项必须有全称；不得为null或空字符串*/
                             Option::ArgRequired,/*该选项后面必须跟值*/
                             nullptr,/*该选项默认值*/
                             true,/*该选项是否必须赋值，如果没有默认值且为ArgRequired时用户必须提供该参数否则将抛异常*/
                             "文件夹或文件",/*该选项说明文字*/
                             nullptr);
    }

    virtual ~CMD_main() {}

    virtual const char *description() const {
        return "添加或删除bom";
    }
};

void get_file_path(const char *path, const char *file_name, char *file_path) {
    strcpy(file_path, path);
    if (file_path[strlen(file_path) - 1] != '/') {
        strcat(file_path, "/");
    }
    strcat(file_path, file_name);
}

template <typename FUNC>
void for_each_file(const char *path, FUNC &&func){
    DIR *dir;
    dirent *dir_info;
    char file_path[PATH_MAX];
    if (File::is_file(path)) {
        func(path);
        return;
    }
    if (File::is_dir(path)) {
        if ((dir = opendir(path)) == NULL) {
            closedir(dir);
            return;
        }
        while ((dir_info = readdir(dir)) != NULL) {
            if (File::is_special_dir(dir_info->d_name)) {
                continue;
            }
            get_file_path(path, dir_info->d_name, file_path);
            for_each_file(file_path,std::forward<FUNC>(func));
        }
        closedir(dir);
        return;
    }
}

static const char s_bom[] = "\xEF\xBB\xBF";

void add_or_rm_bom(const char *file,bool rm_bom){
    auto file_str = File::loadFile(file);
    if(rm_bom){
        file_str.erase(0, sizeof(s_bom) - 1);
    }else{
        file_str.insert(0,s_bom,sizeof(s_bom) - 1);
    }
    File::saveFile(file_str,file);
}

void process_file(const char *file,bool rm_bom){
    std::shared_ptr<FILE> fp(fopen(file, "rb+"), [](FILE *fp) {
        if (fp) {
            fclose(fp);
        }
    });

    if (!fp) {
        WarnL << "打开文件失败:" << file << " " << get_uv_errmsg();
        return;
    }

    bool have_bom = rm_bom;
    char buf[sizeof(s_bom) - 1] = {0};

    if (sizeof(buf) == fread(buf,1,sizeof(buf),fp.get())) {
        have_bom = (memcmp(s_bom, buf, sizeof(s_bom) - 1) == 0);
    }

    if (have_bom == !rm_bom) {
//        DebugL << "无需" << (rm_bom ? "删除" : "添加") << "bom:" << file;
        return;
    }

    fp = nullptr;
    add_or_rm_bom(file,rm_bom);
    InfoL << (rm_bom ? "删除" : "添加") << "bom:" << file;
}

int main(int argc, char *argv[]) {
    CMD_main cmd_main;
    try {
        cmd_main.operator()(argc, argv);
    } catch (std::exception &ex) {
        cout << ex.what() << endl;
        return -1;
    }

    bool rm_bom = cmd_main.hasKey("rm");
    string path = cmd_main["in"];
    string filter = cmd_main["filter"];
    auto vec = split(filter,",");

    set<string> filter_set;
    for(auto ext : vec){
        filter_set.emplace(ext);
    }

    bool no_filter = filter_set.find("*") != filter_set.end();
    //设置日志
    Logger::Instance().add(std::make_shared<ConsoleChannel>());

    for_each_file(path.data(),[&](const char *path){
        if(!no_filter){
            //开启了过滤器
            auto pos = strstr(path,".");
            if(pos == nullptr){
                //没有后缀
                return;
            }
            auto ext = pos + 1;
            if(filter_set.find(ext) == filter_set.end()){
                //后缀不匹配
                return;
            }
        }
        //该文件匹配
        process_file(path,rm_bom);
    });
    return 0;
}
