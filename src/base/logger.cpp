#ifndef LOGGER_SPRINTF_BUF_SIZE
	#define LOGGER_SPRINTF_BUF_SIZE 10240
#endif

#include <stdarg.h>
#include <strings.h>
#define KRLOGGER_BUILDING //sets DLLIMPEXPs in logger.h to 'export' mode
#include "logger.h"
#include "loggerFile.h"
#include "loggerConsole.h"
#include "../stringUtils.h" //needed for parsing the KRLOG env variable

#ifdef _WIN32
#if !defined(va_copy) && defined(_MSC_VER)
	#define va_copy(d,s) ((d) = (s))
#endif
///windows doesn't have the _r function, but the non _r one is thread safe.
///we map the _r to non _r. NOTE: The caller must use the returned pointer,
///not directly the passed-in struct tm, since it is a dummy here and is not
///used here
    inline struct tm *gmtime_r(const time_t *timep, struct tm *result)
    { return gmtime(timep); }
#endif

//this must be in sync with the enums in logger.h
typedef const char* KarereLogLevelName[2];
extern "C" KRLOGGER_DLLEXPORT KarereLogLevelName krLogLevelNames[krLogLevelLast+1] =
{
    {NULL, "off"}, {"ERR", "error"}, {"WRN", "warn"}, {"nfo", "info"},
    {"vrb", "verbose"}, {"dbg", "debug"},{"dbg", "debugv"}
};

namespace karere
{
/** Copies maximum maxCount chars from src to dest.
* @returns number of chars copied, excluding the terminating zero.
* Zero termination is guaranteed in all cases, even if string is truncated
* In this case, the function returns maxCount-1, as the last character is
* the terminating zero and it is not counted
*/
static size_t myStrncpy(char* dest, const char* src, size_t maxCount);

void Logger::logToConsole(bool enable)
{
    if (enable)
    {
        if (mConsoleLogger)
            return;
        mConsoleLogger.reset(new ConsoleLogger(*this));
    }
    else
    {
        if (!mConsoleLogger)
            return;
        mConsoleLogger.reset();
    }
}

void Logger::logToFile(const char* fileName, size_t rotateSizeKb)
{
    if (!fileName) //disable
    {
        mFileLogger.reset();
        return;
    }
    //re-configure
    mFileLogger.reset(new FileLogger(mFlags, fileName, rotateSizeKb*1024));
}

void Logger::setAutoFlush(bool enable)
{
    if (enable)
        mFlags &= ~krLogNoAutoFlush;
    else
        mFlags |= krLogNoAutoFlush;
}

Logger::Logger(unsigned aFlags, const char* timeFmt)
    :mTimeFmt(timeFmt), mFlags(aFlags)
{
    setup();
    setupFromEnvVar();
}

inline size_t Logger::prependInfo(char* buf, size_t bufSize, const char* prefix, const char* severity)
{
    size_t bytesLogged = 0;
    if ((mFlags & krLogNoTimestamps) == 0)
    {
        buf[bytesLogged++] = '[';
        time_t now = time(NULL);
        struct tm tmbuf;
        struct tm* tmval = gmtime_r(&now, &tmbuf);
        bytesLogged += strftime(buf+bytesLogged, bufSize-bytesLogged, mTimeFmt.c_str(), tmval);
        buf[bytesLogged++] = ']';
    }
    if (severity)
    {
        buf[bytesLogged++] = '[';
        bytesLogged += myStrncpy(buf+bytesLogged, severity, bufSize-bytesLogged);
        buf[bytesLogged++] = ']';
    }
    if (prefix)
    {
        buf[bytesLogged++] = '[';
        bytesLogged += myStrncpy(buf+bytesLogged, prefix, bufSize-bytesLogged);
        buf[bytesLogged++] = ']';
    }
    if (bytesLogged)
        buf[bytesLogged++] = ' ';
    return bytesLogged;
}

void Logger::logv(const char* prefix, unsigned level, unsigned flags, const char* fmtString,
                 va_list aVaList)
{
    char statBuf[LOGGER_SPRINTF_BUF_SIZE];
    char* buf = statBuf;
    size_t bytesLogged = prependInfo(buf, LOGGER_SPRINTF_BUF_SIZE, prefix,
        (flags & krLogNoLevel)?NULL:krLogLevelNames[level][0]);

    va_list vaList;
    va_copy(vaList, aVaList);
    int sprintfSpace = LOGGER_SPRINTF_BUF_SIZE-2-bytesLogged;
    int sprintfRv = vsnprintf(buf+bytesLogged, sprintfSpace, fmtString, vaList); //maybe check return value
    if (sprintfRv < 0) //nothing logged if zero, or error if negative, silently ignore the error and return
    {
        va_end(vaList);
        return;
    }
    if (sprintfRv >= sprintfSpace)
    {
        //static buffer was not enough for the message! Message was truncated
        va_copy(vaList, aVaList); //reuse the arg list. GCC printf invalidaes the arg_list after its used
        size_t bufSize = sprintfRv+bytesLogged+2;
        sprintfSpace = sprintfRv+1;
        buf = new char[bufSize];
        if (!buf)
        {
            va_end(vaList);
            fprintf(stderr, "Logger: ERROR: Out of memory allocating a buffer for sprintf");
            return;
        }
        memcpy(buf, statBuf, bytesLogged);
        sprintfRv = vsnprintf(buf+bytesLogged, sprintfSpace, fmtString, vaList); //maybe check return value
        if (sprintfRv >= sprintfSpace)
        {
            perror("Error: vsnprintf wants to write more data than the size of buffer it requested");
            sprintfRv = sprintfSpace-1;
        }
    }
    va_end(vaList);
    bytesLogged+=sprintfRv;
    buf[bytesLogged] = '0';
    logString(level, buf, flags, bytesLogged);
}

/** This is the low-level log function that does the actual logging
 *  of an assembled single string. We still need the log level here, because if the
 *  console color selection.
 */
void Logger::logString(unsigned level, const char* msg, unsigned flags, size_t len)
{
    if (len == (size_t)-1)
        len = strlen(msg);

    std::lock_guard<std::mutex> lock(mMutex);
    flags |= (mFlags & krGlobalFlagMask);
    if (mConsoleLogger && ((flags & krLogNoFile) == 0))
        mConsoleLogger->logString(level, msg, flags);
    if ((mFileLogger) && ((flags && krLogNoConsole) == 0))
        mFileLogger->logString(msg, len, flags);
    if (!mUserLoggers.empty())
    {
        for (auto& logger: mUserLoggers)
            logger.second->log(level, msg, len, flags);
    }
}

void Logger::log(const char* prefix, unsigned level, unsigned flags,
                const char* fmtString, ...)
{
    va_list vaList;
    va_start(vaList, fmtString);
    logv(prefix, level, flags, fmtString, vaList);
    va_end(vaList);
}

std::shared_ptr<Logger::LogBuffer> Logger::loadLog()
{
    if (!mFileLogger)
        return NULL;
    std::lock_guard<std::mutex> lock(mMutex);
    return mFileLogger->loadLog();
}

Logger::~Logger()
{}

void Logger::addUserLogger(const char* tag, ILoggerBackend* logger)
{
    std::lock_guard<std::mutex> lock(mMutex);
    mUserLoggers[tag].reset(logger);
}
bool Logger::removeUserLogger(const char* tag)
{
    std::lock_guard<std::mutex> lock(mMutex);
    auto item = mUserLoggers.find(tag);
    if (item == mUserLoggers.end())
        return false;
    mUserLoggers.erase(item);
    return true;
}

void Logger::setupFromEnvVar()
{
    const char* strConfig = getenv("KRLOG");
    if (!strConfig)
        return;
    struct ParamVal: public std::string
    {
        unsigned numVal;
        ParamVal(std::string&& str): std::string(std::forward<std::string>(str)){};
    };

    std::map<std::string, ParamVal> config;
    try
    {
        parseNameValues(strConfig, " ;:", '=', config);
        //verify log level names
        for (auto& param: config)
        {
            unsigned level = krLogLevelStrToNum(param.second.c_str());
            if (level == (unsigned)-1)
                throw std::runtime_error("can't recognize log level name '"+param.second+"'");
            param.second.numVal = level;
        }
    }
    catch(std::exception& e)
    {
        log("LOGGER", krLogLevelError, 0, "Error parsing KRLOG env variable: %s. Settings from that variable will not be applied", e.what());
        return;
    }

    //put channels in a map for easier access
    unsigned allLevels;
    auto it = config.find("all");
    if(it != config.end()) {
        allLevels = it->second.numVal;
        config.erase(it);
    }
    else
    {
        allLevels = (unsigned)-1;
    }
    std::map<std::string, KarereLogChannel*> chans;
    for (size_t n = 0; n < krLogChannelLast; n++)
    {
        KarereLogChannel& chan = logChannels[n];
        if (allLevels != (unsigned)-1)
            chan.logLevel = allLevels;
        chans[chan.id] = &chan;
    }
    for (auto& item: config)
    {
        auto chan = chans.find(item.first);
        if (chan == chans.end())
            log("LOGGER", krLogLevelError, 0, "Unknown channel in KRLOG env variable: %s. Ignoring", item.first.c_str());
        chan->second->logLevel = item.second.numVal;
    }
}

Logger gLogger;
extern "C" KRLOGGER_DLLEXPORT KarereLogChannel* krLoggerChannels = gLogger.logChannels;
extern "C" KRLOGGER_DLLEXPORT unsigned krLogLevelStrToNum(const char* strLevel)
{
    for (size_t n = 0; n<=krLogLevelLast; n++)
    {
        auto& name = krLogLevelNames[n];
        if ((strcasecmp(strLevel, name[1]) == 0)
         || (name[0] && (strcasecmp(strLevel, name[0]) == 0)))
            return n;
    }
    return (unsigned)-1;
}

static size_t myStrncpy(char* dest, const char* src, size_t maxCount)
{
    size_t count = 1;
    const char* sptr = src;
    char* dptr = dest;
    for ( ;count <= maxCount; sptr++, dptr++, count++)
    {
        *dptr = *sptr;
         if (*sptr == 0)
            break;
    }
    if (count > maxCount) //copy ermianted because we reached maxCount
    {
        dptr[maxCount-1] = 0; //guarantee zero termination even if string is truncated
        return maxCount-1; //we ate the last char to put te terinating zero there
    }
    return count-1;
}
}

extern "C" KRLOGGER_DLLEXPORT void krLoggerLog(unsigned channel, unsigned level,
    const char* fmtString, ...)
{
    va_list vaList;
    va_start(vaList, fmtString);
    auto chan = karere::gLogger.logChannels[channel];
    karere::gLogger.logv(chan.display, level, chan.flags, fmtString, vaList);
    va_end(vaList);
}