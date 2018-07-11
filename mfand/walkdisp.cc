#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>

#include "walkdisp.h"

int32_t
WalkTask::start()
{
    DIR *dirp;
    struct stat tstat;
    int32_t code;
    struct dirent *entryp;
    WalkTask *childTaskp;
#ifndef __linux__
    char tbuffer[1024];
#endif

    CDisp *disp = getDisp();

    code = stat(_path.c_str(), &tstat);
    if (code < 0)
        return code;
    if ((tstat.st_mode & S_IFMT) == S_IFREG) {
        if (_callbackProcp) {
            _callbackProcp(_callbackContextp, &_path, &tstat);
        }
        return 0;
    }
    else if ((tstat.st_mode & S_IFMT) == S_IFDIR) {
        dirp = opendir(_path.c_str());
        if (!dirp)
            return -1;

        /* process the dir */
        if (_callbackProcp) {
            _callbackProcp(_callbackContextp, &_path, &tstat);
        }

        while(1) {
#ifdef __linux__
            entryp = readdir(dirp);
            if (!entryp)
                break;
#else
            code = readdir_r(dirp, (struct dirent *)tbuffer, &entryp);
            if (code)
                break;
            if (!entryp)
                break;
#endif

            /* skip . and .. */
#ifdef __linux__
            if ( strcmp(entryp->d_name, ".") == 0 ||
                 strcmp(entryp->d_name, "..") == 0)
                continue;
#else
            if (entryp->d_namlen == 1 && strncmp(entryp->d_name, ".", 1) == 0)
                continue;
            if (entryp->d_namlen == 2 && strncmp(entryp->d_name, "..", 2) == 0)
                continue;
#endif

            childTaskp = new WalkTask();
#ifdef __linux__
            childTaskp->initWithPath(_path + "/" + std::string(entryp->d_name));
#else
            childTaskp->initWithPath(_path + "/" + std::string(entryp->d_name, entryp->d_namlen));
#endif
            childTaskp->setCallback(_callbackProcp, _callbackContextp);
            disp->queueTask(childTaskp);
        }

        closedir(dirp);
        return code;
    }
    else {
        if (_callbackProcp) {
            _callbackProcp(_callbackContextp, &_path, &tstat);
        }
        return 0;
    }
}
