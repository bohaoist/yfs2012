# yfs2012
#6.824 Lab 1: Lock Server


##简介
整个的2012年MIT 6.824 课程实验需要完成一个分布式的文件系统, 而文件系统结构的更新需要在锁的保护下进行.所以本次实验(lab1)是为了实现一个简单的锁服务. 

锁服务的核心逻辑包含两个模块:**锁客户端**和**锁服务器**.
两个模块之间通过RPC(远程过程调用)进行通信. 当**锁客户端**请求一个特定的锁时,它向**锁服务器**发送一个**acquire请求**.**锁服务器**确保这个锁一次只被一个**锁客户端**获取. 当**锁客户端**获取到锁,完成相关的操作后,通过给**锁服务器**发送一个**release请求**,这样**锁服务器**可以使得该锁被其他等待该锁的**锁客户端**获取. 在本次实验中除了实现锁服务,还需要给RPC库增加消除重复RPC请求的功能确保*at-most-once*执行. 因为在网络环境下RPC请求可能被丢失,所以RPC系统必须重传丢失的RPC.但是有些情形下原来的RPC并没有丢失,但是RPC系统却认为已经丢失了,结果进行了重传,导致重复的RPC请求. 下面是一个例子介绍重复的RPC请求带来的不正确的行为.**锁客户端**为了获取锁X,给**锁服务器**发送一个**acquire 请求**. **锁服务器**确保锁X被该客户端获取.然后**锁客户端**通过一个**release请求**释放锁X.但是这时一个重复的RPC请求到达**锁客户端**要求获取锁X. **锁服务器**确保该锁被获取,但是**锁客户端**绝不会释放锁X. 因为这个请求只是第一次**acquire请求**的副本.
## 实验内容
实验内容分为两部分: 
1. 提供基本锁操作**acquire**和**release**.
2. 考虑重复RPC请求.消除重复RPC请求带来的错误
下面分两个部分进行介绍
### 第一部分
在这部分中不需要考虑网络带来的重复RPC,假设网络是完美的.仅仅需要实现基本的锁操作**acquire**和**release**. 并且必须遵守一个不变量:**任何时间点,同一个锁不能被两个或者以上的锁客户端持有**
下面介绍实现过程. 
lock_smain.cc包含锁服务的基本过程.其中定义一个lock_server ls,然后在RPC服务中登记各种请求对应的handler. 我们需要实现**acquire**和**release**,因此也需要在RPC服务中添加对应的handler,在lock_smain.cc中增加相应的代码,如下:
`lock_server ls; 
rpcs server(atoi(argv[1]), count);
server.reg(lock_protocol::stat, &ls, &lock_server::stat);
  server.reg(lock_protocol::acquire, &ls, &lock_server::acquire);
  server.reg(lock_protocol::release, &ls, &lock_server::release);`
**锁客户端**的请求**lock_protocol::acquire**和**lock_protocol::release**
在**锁服务器**中相应的handler是**lock_server::acquire**和**lock_server::release**. 

上面建立了RPC请求和handler的关系,但是实验给出的代码中没有给出相应的锁的定义,因此我们需要自定义锁. 在lock_protocol.h中添加锁的定义(也可以新建单独的文件在其中定义锁,但是这需要修改GNUMakefile来包含新文件).
`class lock {
 public: 
    enum lock_status {FREE, LOCKED};
    lock_protocol::lockid_t lid;
    int status;
    pthread_cond_t lcond;
    lock(lock_protocol::lockid_t);
    lock(lock_protocol::lockid_t, int);
    ~lock(){};
    };`
其中
* lid 表示锁的id,用来唯一的标示锁.
* stauts 表示锁的状态,FREE或者LOCKED.
* lcond 是一个条件变量,当锁是LOCKED状态时,其它要获取该锁的线程必须等待.
当锁的状态变为FREE时,需要唤醒这些等待的进程.

RPC系统维护了线程池,当收到一个请求后,从线程池中选择一个空闲线程执行请求对应的handler. 因此会有多个线程并发请求锁的情形. 同时**锁服务器**维护的
锁的个数是开放的,任意增长的,当**锁客户端**请求一个从未有过的锁时,就创建一个新的锁. 在lock_server.h中我们增加了一个数据结构lockmap, 记录**锁服务器**
维护的所有锁.同时一个互斥量mutex,用来保证多线程并发的访问时不会出错.
`class lock_server {
protected:
  int nacquire;
  pthread_mutex_t mutex;
  std::map<lock_protocol::lockid_t, lock* > lockmap;
 public:
  lock_server();
  ~lock_server() {}; 
  lock_protocol::status stat(int clt, lock_protocol::lockid_t lid, int &); 
  lock_protocol::status acquire(int clt, lock_protocol::lockid_t lid, int &); 
  lock_protocol::status release(int clt, lock_protocol::lockid_t lid, int &); 
};`
在lock_server结构中我们还看了**acquire**和**release**的操作,这两个操作
在前面的介绍中已经作为handler登记到RPC系统中. 这两个函数参数信息包含
* clt: 锁客户端id, 用来标示**锁客户端**
* lid: 所请求的锁的id.用来表示锁
* 第三个参数是一个结果信息.
当**锁客户端**的请求到来后,RPC系统就从线程池中找一个空闲的线程执行
对应handler,可能是lock_server中的**acquire**或者**release**.
下面介绍这两个函数的实现. 在lock_server.cc中**acquire**的实现如下:
`lock_protocol::status
lock_server::acquire(int clt, lock_protocol::lockid_t lid, int &r) 
{
    lock_protocol::status ret = lock_protocol::OK;
    std::map<lock_protocol::lockid_t, lock* >::iterator iter;
    pthread_mutex_lock(&mutex);
    iter = lockmap.find(lid);
    if(iter != lockmap.end()) {
        while(iter->second->status != lock::FREE) {
            pthread_cond_wait(&(iter->second->lcond), &mutex);
        }
        iter->second->status = lock::LOCKED;
        pthread_mutex_unlock(&mutex);
        return ret;
    } else {
        lock *new_lock = new lock(lid, lock::LOCKED);
    //  lockmap[lid] = new_lock;
        lockmap.insert(std::make_pair(lid, new_lock));
        pthread_mutex_unlock(&mutex);
        return ret;
    }   
}`
因为多线程需要互斥的访问共享的数据结构lockmap.所以首先需要获取mutex.
然后在lockmap中查询lid对应的锁的状态,如果是LOCKED,那么当前线程在该锁
的条件变量lcond上阻塞直到锁被释放,但是线程将被唤醒,然后在while循环中检测锁的状态,直到可以获取该锁.如果锁的状态是FREE,就直接将锁的状态修改为LOCKED,表示获取该锁. 如果请求的锁不存在,就创建一个新锁加入到lockmap中, 并确保新创建的锁被**锁客户端**获取. 

对应**锁服务器**中**release**的实现如下
`lock_protocol::status
lock_server::release(int clt, lock_protocol::lockid_t lid, int &r) 
{
    lock_protocol::status ret = lock_protocol::OK;
    std::map<lock_protocol::lockid_t, lock*>::iterator iter;
    pthread_mutex_lock(&mutex);
    iter = lockmap.find(lid);
    if (iter != lockmap.end()) {
        iter->second->status = lock::FREE;
        pthread_cond_signal(&(iter->second->lcond));
        pthread_mutex_unlock(&mutex);
        return ret;
    } else {
        ret = lock_protocol::IOERR;
        pthread_mutex_unlock(&mutex);
        return ret;
    }
}`
**release**的实现相对很简单,在lockmap中查询对应的锁,然后将锁的状态置为
FREE,然后唤醒等待该锁的线程.

以上是**acquire**和**release**在**锁服务器**端的实现,下面介绍这两个操作在**锁客户端**的实现.参照客户端stat的写法.在lock_client.cc中添加如下代码
`lock_protocol::status
lock_client::acquire(lock_protocol::lockid_t lid)
{
    int r;
    lock_protocol::status ret = cl->call(lock_protocol::acquire, cl->id(), lid, r); 
    VERIFY(ret == lock_protocol::OK);
    return r;
}`

`lock_protocol::status
lock_client::release(lock_protocol::lockid_t lid)
{
    int r;
    lock_protocol::status ret = cl->call(lock_protocol::release, cl->id(), lid, r); 
    VERIFY(ret == lock_protocol::OK);
    return r;
}`
到此实验的第一部分完成,可以通过lock_tester的测试.
###第二部分
这里主要考虑消除重复RPC带来的错误,确保*at-most-once*执行.
一种方法是在服务器端记录所有已经接收到的RPC请求,每个RPC请求由xid和clt_nonce标示,clt_nonce用来标示是哪个客户端,xid标示该客户端发送的特定的一个请求.除了记录RPC的标示,还需要记录每个RPC的处理结果,这样当重复的RPC请求到来时,重发这个结果就行.这种方法确保"at-most-once",但是记录这些信息的内存消耗几乎是无限增长的. 另一种方法是滑动窗口.服务器端只记录部分的RPC请求信息,而不是全部,而且要求客户端的xid必须是严格的递增,如0,1,2,3... 当服务器端接收到一个请求,该请求中包含三个信息(xid, clt_nonce, xid_rep)
xid和clt_nonce前面已经介绍,用来标示一个请求,而xid_rep的意思是告诉服务器
端xid_rep之前的请求客户端都已经收到了回复.所以服务器端不需要在保存xid_rep之前的信息了,可以删除. 服务器收到请求(xid,clt_nonce,xid_rep)后,将该请求
在窗口中查询:
1. 如果窗口中不存在这个请求,表示这是一个新请求.那么将该请求加入到窗口,同时删除xid_rep之前的请求,然后调用对应的handler处理这个新请求.
然后处理的结果存入窗口(存储结果的过程由add_reply函数完成).
2. 如果这个请求在窗口中已经存在.说明现在的请求是一个重复的请求,不需要调用handler.直接在窗口中查找这个请求的结果. 然后结果还没准备好,说明handler还在处理这个请求. 如果结果已经存在,说明handler已经处理完了这个请求,直接将结果再重发给客户端.
3. 如果窗口中不存在这个请求,并且xid小于客户端clt_nonce对应的窗口中最小的xid.说明这个请求已经被从窗口中删除.
上面三个过程由rpc/rpc.cc中checkduplicate_and_update函数完成.这部分代码
需要我们编写. 实现如下:
`rpcs::rpcstate_t
rpcs::checkduplicate_and_update(unsigned int clt_nonce, unsigned int xid,
        unsigned int xid_rep, char **b, int *sz)
{
    ScopedLock rwl(&reply_window_m_);
    std::list<reply_t>::iterator iter;
    for (iter = reply_window_[clt_nonce].begin(); iter != reply_window_[clt_nonce].end(); ) {
        if (iter->xid < xid_rep && iter->cb_present) {
            free(iter->buf);
            iter = reply_window_[clt_nonce].erase(iter);
            continue;
        }
        if (xid == iter->xid) {
            if(iter->cb_present) {
                *b = iter->buf;
                *sz = iter->sz;
                return DONE;
            } else {
                return INPROGRESS;
            }
        }
        if(reply_window_[clt_nonce].front().xid > xid)
            return FORGOTTEN;
        iter++;
    }
    reply_t reply(xid);
    for (iter = reply_window_[clt_nonce].begin(); iter != reply_window_[clt_nonce].end(); iter++) {
        if(iter->xid > xid) {
            reply_window_[clt_nonce].insert(iter, reply);
            break;
        }
    }
    if(iter == reply_window_[clt_nonce].end())
        reply_window_[clt_nonce].push_back(reply);
    return NEW;
        // You fill this in for Lab 1.
}`
iter->cb_present表示结果是否有效.如果有效则返回DONE.如果无效表示还在处理,返回INPROGRESS. 如果xid比clt_nonce对的窗口中最小的xid还要xiao,说明
这个请求已经被删除.然会FORGOTTEN. 对于新的请求则按序插入到窗口中. 
第一个for循环中已经将xid_rep前面的请求删除.

我们还需要实现另一个函数add_reply,这个函数的作用是将一个请求的结果保存在
窗口中.在rpc/rpc.cc中add_reply实现如下:
`void
rpcs::add_reply(unsigned int clt_nonce, unsigned int xid,
        char *b, int sz)
{
    ScopedLock rwl(&reply_window_m_);
    std::map<unsigned int, std::list<reply_t> >::iterator clt;
    std::list<reply_t>::iterator iter;
    clt = reply_window_.find(clt_nonce);
    if (clt != reply_window_.end()) {
        for (iter = clt->second.begin(); iter != clt->second.end(); iter++) {
            if (iter->xid == xid) {
                iter->buf = b;
                iter->sz = sz;
                iter->cb_present = true;
                break;
            }
        }
    }
        // You fill this in for Lab 1.
}`
在窗口中找到对应的请求(这个请求是在checkduplicate_and_update中加入到窗口的,但是结果还未有效,handler还在处理),然后保存将结果保存.并且置cb_preset为true.表示结果有效. 

最后测试./rpc/rpctest和lock_tester.在网络有丢失的情形下,测试成功.
