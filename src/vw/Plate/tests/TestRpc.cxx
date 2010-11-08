// __BEGIN_LICENSE__
// Copyright (C) 2006-2010 United States Government as represented by
// the Administrator of the National Aeronautics and Space Administration.
// All Rights Reserved.
// __END_LICENSE__


#include <gtest/gtest.h>
#include <test/Helpers.h>
#include <vw/Plate/RpcChannel.h>
#include <vw/Plate/HTTPUtils.h>
#include <vw/Plate/Exception.h>
#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>
#include <cstdlib>

using namespace std;
using namespace vw;
using namespace vw::platefile;

typedef boost::shared_ptr<IChannel> Chan;

const std::string default_hostname = "localhost";
const std::string default_port = "5672";

const uint64 TIMEOUT = 1000;

string env_str(const char *key, const string& def) {
  const char *val = getenv(key);
  return val ? val : def;
}

Url amqp_url(string hostname = "", short port = -1) {
  if (hostname.empty())
    hostname = env_str("AMQP_TEST_HOSTNAME", default_hostname);

  string sport = env_str("AMQP_TEST_PORT", default_port);
  if (port != -1) {
    sport = boost::lexical_cast<string>(port);
  }
  return string("amqp://") + hostname + ":" + sport + "/unittest/server";
}

struct GenClient {
  const Url& url;
  uint64 num;
  GenClient(const Url& url) : url(url), num(0) {}
  Chan operator()() {
    Chan ret(IChannel::make_conn(url, "unittest_client" + stringify(num++)));
    ret->set_timeout(TIMEOUT);
    return ret;
  }
};

struct IChannelTest : public ::testing::TestWithParam<Url> {
  ByteArray e1,e2;
  Chan server;
  vector<Chan> clients;

  void SetUp() {
    static const char m1[] = "13", m2[] = "26";
    e1 = ByteArray(m1, m1+sizeof(m1));
    e2 = ByteArray(m2, m2+sizeof(m2));
    ASSERT_NO_THROW(server.reset(IChannel::make_bind(GetParam(), "unittest_server")));
    server->set_timeout(TIMEOUT);
    clients.resize(0);
  }

  void make_clients(uint64 count) {
    clients.resize(count);
    ASSERT_NO_THROW(std::generate(clients.begin(), clients.end(), GenClient(GetParam())));
  }

  void TearDown() {
    clients.resize(0);
    server.reset();
  }
};

TEST_P(IChannelTest, Request) {
  SharedByteArray a1;
  make_clients(1);

  clients[0]->send_bytes(e1.begin(), e1.size());
  server->recv_bytes(a1);

  ASSERT_TRUE(a1);
  EXPECT_RANGE_EQ(e1.begin(), e1.end(), a1->begin(), a1->end());
}

TEST_P(IChannelTest, RequestReply) {
  SharedByteArray a1, a2;
  make_clients(1);

  clients[0]->send_bytes(e1.begin(), e1.size());
  ASSERT_TRUE(server->recv_bytes(a1));
  ASSERT_TRUE(a1);
  EXPECT_RANGE_EQ(e1.begin(), e1.end(), a1->begin(), a1->end());

  server->send_bytes(e2.begin(), e2.size());
  ASSERT_TRUE(clients[0]->recv_bytes(a2));
  ASSERT_TRUE(a2);
  EXPECT_RANGE_EQ(e2.begin(), e2.end(), a2->begin(), a2->end());
}

struct NumberTask {
  struct Msg {
    uint64 id;
    uint64 num;
    Msg(uint64 id, uint64 num) : id(id), num(num) {}
  };

  const Url& url;
  uint64 id;
  bool done;
  std::vector<Msg> received;
  static const uint64 COUNT = 1000;

  NumberTask(const Url& url) : url(url), done(false) {}

  Msg unmake(const ByteArray& b) {
    Msg m(-1,-1);
    VW_ASSERT(b.size() == sizeof(m), LogicErr() << "Error in message size");
    ::memcpy(&m, b.begin(), sizeof(m));
    return m;
  }

  void operator()() {
    id = Thread::id();
    ASSERT_NE(0, id) << "None of the threads should be thread 0";
    Chan client(IChannel::make_conn(url, "unittest_client" + stringify(id)));
    client->set_timeout(TIMEOUT);

    received.resize(COUNT, Msg(-1,-1));
    SharedByteArray in;
    for (uint64 i = 0; i < COUNT; ++i) {
      Msg out(id, i);
      client->send_bytes(reinterpret_cast<const uint8*>(&out), sizeof(Msg));
      ASSERT_TRUE(client->recv_bytes(in));
      ASSERT_TRUE(in);
      received[i] = unmake(*in);
    }
    done = true;
  }
};

TEST_P(IChannelTest, MultiThreadTorture) {
  uint64 COUNT = 30;

  typedef boost::shared_ptr<NumberTask> task_t;
  typedef boost::shared_ptr<Thread> thread_t;

  vector<task_t>   tasks(COUNT);
  vector<thread_t> threads(COUNT);

  EXPECT_EQ(Thread::id(), 0);

  for (uint64 i = 0; i < COUNT; ++i) {
    tasks[i]   = task_t(new NumberTask(GetParam()));
    threads[i] = thread_t(new Thread(tasks[i]));
  }

  uint64 msgs = 0;
  SharedByteArray msg;
  while (1) {
    msg.reset();
    if (!server->recv_bytes(msg))
      break;
    ASSERT_TRUE(msg);
    server->send_bytes(msg->begin(), msg->size());
    msgs++;
  }

  BOOST_FOREACH(thread_t& t, threads)
    t->join();

  ASSERT_EQ(COUNT * NumberTask::COUNT, msgs);

  BOOST_FOREACH(task_t& t, tasks) {
    uint64 i = 0;
    BOOST_FOREACH(const NumberTask::Msg& msg, t->received) {
      EXPECT_EQ(t->id, msg.id);
      EXPECT_EQ(i,     msg.num);
      i++;
    }
  }
}

#if 0
// RpcServer implements a CallMethod (which talks over the IChannel). It
// delegates the center to ServiceT::CallMethod.
template <typename ServiceT>
class RpcServer : public ServiceT {
  protected:
    Chan m_channel;

  public:
    RpcServer(Chan& channel) : channel(channel) {}
    RpcServer(const string& url) : channel(make_bind(url)) {}

    void run()

    void CallMethod(
        const ::google::protobuf::MethodDescriptor* method,
        IController* controller,
        const ::google::protobuf::Message* request,
        ::google::protobuf::Message* response,
        ::google::protobuf::Closure* done) {
    }

}

class TestServiceImpl : public RpcServer<TestService> {
  TestServiceImpl(
}

TEST_P(IChannelTest, BasicRpc) {
  make_clients(1);

   IController controller;

   TestServiceImpl server_svc(&
   TestServiceImpl::Stub stub(&client[0])
   FooRequest request;
   FooRespnose response;
}
#endif

INSTANTIATE_TEST_CASE_P(URLs, IChannelTest,
                        ::testing::Values(
                           amqp_url()
                         , "zmq+ipc://" TEST_OBJDIR "/unittest"
                         , "zmq+tcp://127.0.0.1:54321"
                         , "zmq+inproc://unittest"
                         ));