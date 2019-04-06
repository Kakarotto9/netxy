#ifndef C_COROUTINE_H
#define C_COROUTINE_H

#ifdef  __cplusplus
extern "C" {
#endif

enum COROUTINE_STATE
{
    COROUTINE_READY,
    COROUTINE_RUNNING,
    COROUTINE_SUSPEND,
    COROUTINE_BLOCK,
    COROUTINE_DEAD,
};

struct schedule;

typedef void (*coroutine_func)(struct schedule *, void *ud);

struct schedule * coroutine_open(void);
void coroutine_close(struct schedule *);

/*  ����Э�̷�����ID   */
int coroutine_new(struct schedule *, coroutine_func, void *ud);

/*  ����������Э��    */
void coroutine_resume(struct schedule *, int id);

/*  ��ȡЭ�̵�״̬ */
int coroutine_status(struct schedule *, int id);

/*  ��ȡ��ǰ���е�Э��ID */
int coroutine_running(struct schedule *);

/*  ����ǰЭ������(������Э��)    */
void coroutine_yield(struct schedule *);

/*  ������ǰЭ��(������Э��)   */
void coroutine_block(struct schedule *);

/*  Э�̵�����   */
void coroutine_schedule(struct schedule*);

#ifdef  __cplusplus
}
#endif

#endif
