#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>

#include "walkdisp.h"

int32_t
WalkTask::start()
{
    DIR *dirp;
    struct stat tstat;
    char tbuffer[1024];
    int32_t code;
    struct dirent *entryp;
    WalkTask *childTaskp;

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
            code = readdir_r(dirp, (struct dirent *)tbuffer, &entryp);
            if (code)
                break;
            if (!entryp)
                break;

            /* skip . and .. */
            if (entryp->d_namlen == 1 && strncmp(entryp->d_name, ".", 1) == 0)
                continue;
            if (entryp->d_namlen == 2 && strncmp(entryp->d_name, "..", 2) == 0)
                continue;

            childTaskp = new WalkTask();
            childTaskp->initWithPath(_path + "/" + std::string(entryp->d_name, entryp->d_namlen));
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
