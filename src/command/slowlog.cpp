/*
 *Copyright (c) 2013-2014, yinqiwen <yinqiwen@gmail.com>
 *All rights reserved.
 * 
 *Redistribution and use in source and binary forms, with or without
 *modification, are permitted provided that the following conditions are met:
 * 
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  * Neither the name of Redis nor the names of its contributors may be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 * 
 *THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS 
 *BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF 
 *THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "ardb.hpp"

namespace ardb
{
    struct SlowLogRecord
    {
            uint64 id;
            uint64 ts;
            uint64 costs;
            RedisCommandFrame cmd;
            SlowLogRecord() :
                    id(0), ts(0), costs(0)
            {
            }
    };
    typedef std::deque<SlowLogRecord> SlowLogQueue;
    static SlowLogQueue g_slowlog_queue;
    static SpinMutexLock g_slowlog_queue_mutex;
    static uint64 kSlowlogIDSeed = 0;

    void Ardb::TryPushSlowCommand(const RedisCommandFrame& cmd, uint64 micros)
    {
        if (micros < m_cfg.slowlog_log_slower_than)
        {
            return;
        }
        LockGuard<SpinMutexLock> guard(g_slowlog_queue_mutex);
        while (m_cfg.slowlog_max_len > 0 && g_slowlog_queue.size() >= (uint32) m_cfg.slowlog_max_len)
        {
            g_slowlog_queue.pop_front();
        }
        SlowLogRecord log;
        log.id = kSlowlogIDSeed++;
        log.costs = micros;
        log.ts = get_current_epoch_micros();
        log.cmd = cmd;
        g_slowlog_queue.push_back(log);
    }

    void Ardb::GetSlowlog(Context& ctx, uint32 len)
    {
        ctx.reply.type = REDIS_REPLY_ARRAY;
        LockGuard<SpinMutexLock> guard(g_slowlog_queue_mutex);
        for (uint32 i = 0; i < len && i < g_slowlog_queue.size(); i++)
        {
            SlowLogRecord& log = g_slowlog_queue[i];
            RedisReply& r = ctx.reply.AddMember();
            RedisReply& rr1 = r.AddMember();
            RedisReply& rr2 = r.AddMember();
            RedisReply& rr3 = r.AddMember();
            fill_int_reply(rr1, log.id);
            fill_int_reply(rr2, log.ts);
            fill_int_reply(rr3, log.costs);

            RedisReply& cmdreply = r.AddMember();

            cmdreply.type = REDIS_REPLY_ARRAY;
            RedisReply& cmdname = cmdreply.AddMember();
            fill_str_reply(cmdname, log.cmd.GetCommand());
            for (uint32 j = 0; j < log.cmd.GetArguments().size(); j++)
            {
                RedisReply& arg = cmdreply.AddMember();
                fill_str_reply(arg, *(log.cmd.GetArgument(j)));
            }
        }
    }

    int Ardb::SlowLog(Context& ctx, RedisCommandFrame& cmd)
    {
        std::string subcmd = string_tolower(cmd.GetArguments()[0]);
        if (subcmd == "len")
        {
            fill_int_reply(ctx.reply, g_slowlog_queue.size());
        }
        else if (subcmd == "reset")
        {
            fill_status_reply(ctx.reply, "OK");
            LockGuard<SpinMutexLock> guard(g_slowlog_queue_mutex);
            g_slowlog_queue.clear();
        }
        else if (subcmd == "get")
        {
            if (cmd.GetArguments().size() != 2)
            {
                fill_error_reply(ctx.reply, "wrong number of arguments for SLOWLOG GET");
            }
            uint32 len = 0;
            if (!string_touint32(cmd.GetArguments()[1], len))
            {
                fill_error_reply(ctx.reply, "value is not an integer or out of range.");
                return 0;
            }
            GetSlowlog(ctx, len);
        }
        else
        {
            fill_error_reply(ctx.reply, "SLOWLOG subcommand must be one of GET, LEN, RESET");
        }
        return 0;
    }
}

