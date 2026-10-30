#include "klq_mock.h"
#include "mini_python.h"
#include "klq_runtime.h"
#include "external_ports.h"
#include "robot_ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
volatile uint32_t board_ms;
static uint8_t ready[3],state[3],running[3];
static klq_port_status_t ports[3];
static unsigned checks,stops,displays;
#define CHECK(x) do {++checks;if(!(x)){printf("FAIL line%u: %s\n",__LINE__,#x);exit(1);}} while(0)
void RCM_EnableAPB2PeriphClock(unsigned v){(void)v;}
void GPIO_Config(void *p,GPIO_Config_T *io){(void)p;(void)io;}
unsigned GPIO_ReadInputBit(void *p,unsigned n){(void)p;(void)n;return 1;}
const klq_port_status_t *external_port_status(unsigned p){return p<3?ports+p:0;}
bool external_port_motor_ready(unsigned p){return p<3&&ready[p]!=0;}
uint8_t external_port_motor_state(unsigned p){return p<3?state[p]:KLQ_MOTOR_ERROR;}
bool external_port_motor(unsigned p,int direction,bool run,unsigned power)
{
    (void)direction;(void)power;CHECK(p<3);
    if(!ready[p])return false;
    state[p]=run?KLQ_MOTOR_PENDING:KLQ_MOTOR_STOPPING;running[p]=(uint8_t)run;return true;
}
void external_ports_motor_stop_all(void)
{
    unsigned i;++stops;
    for(i=0;i<3;i++){if(running[i])state[i]=KLQ_MOTOR_STOPPING;running[i]=0;}
}
bool robot_ui_set_user_pattern(const uint8_t *p){(void)p;++displays;return true;}
static void begin(const char *source)
{mini_python_init((const uint8_t *)source,(uint32_t)strlen(source));CHECK(mini_python_start());}
static void setup(const char *source)
{
    board_ms=100;stops=displays=0;memset(ready,1,sizeof(ready));memset(state,0,sizeof(state));memset(running,0,sizeof(running));memset(ports,0,sizeof(ports));
    klq_runtime_init();begin(source);
}
static void step(void){CHECK(mini_python_poll()==MINI_RUNNING);}
static void fault(unsigned line)
{
    CHECK(mini_python_poll()==MINI_ERROR);CHECK(mini_python_error()==MINI_ERR_RUNTIME);CHECK(mini_python_error_line()==line);
    CHECK(stops==1&&!running[0]&&!running[1]&&!running[2]);CHECK(!displays);CHECK(klq_runtime_motors_ok());
}
static void done(void)
{
    unsigned i=0;while(mini_python_status()==MINI_RUNNING&&i++<30)mini_python_poll();
    CHECK(mini_python_status()==MINI_DONE&&mini_python_error()==MINI_ERR_NONE);
}
static void waits(void)
{
    unsigned offline;
    for(offline=0;offline<2;offline++){
        setup("klq.motor_run(2,1)\nklq.wait(100)\nklq.display_number(9)\n");step();state[1]=KLQ_MOTOR_ACTIVE;step();
        if(offline)ready[1]=0;else state[1]=KLQ_MOTOR_ERROR;
        fault(2);CHECK(board_ms==100);
        setup("klq.motor_for(3,-1,100)\nklq.display_number(9)\n");step();
        if(offline){state[2]=KLQ_MOTOR_ACTIVE;ready[2]=0;}else state[2]=KLQ_MOTOR_ERROR;
        fault(1);CHECK(board_ms==100);
        setup("klq.motor_run(1,1)\nwhile True:\n    pass\n");step();state[0]=KLQ_MOTOR_ACTIVE;step();step();
        if(offline)ready[0]=0;else state[0]=KLQ_MOTOR_ERROR;
        fault(3);CHECK(board_ms==100);
    }
    puts("PASS ERROR/offline during WAIT_TIME, motor_for and no-delay loop exits before wait/deadline");
}
static void healthy(void)
{
    unsigned i;
    setup("klq.motor_for(1,1,0.1)\nklq.display_number(9)\n");step();
    for(i=0;i<3;i++){++board_ms;step();CHECK(state[0]==KLQ_MOTOR_PENDING&&!displays&&!stops);}
    state[0]=KLQ_MOTOR_ACTIVE;board_ms=199;step();CHECK(running[0]&&!displays);board_ms=200;step();
    CHECK(state[0]==KLQ_MOTOR_STOPPING&&!running[0]&&displays==1);done();
    setup("klq.motor_run(1,1)\nklq.motor_stop(1)\nklq.wait(1)\nklq.display_number(9)\n");step();state[0]=KLQ_MOTOR_ACTIVE;step();step();
    for(i=0;i<10;i++){step();CHECK(state[0]==KLQ_MOTOR_STOPPING&&!stops&&!displays);}
    state[0]=KLQ_MOTOR_IDLE;step();state[0]=KLQ_MOTOR_ERROR;ready[0]=0;step();
    CHECK(mini_python_error()==MINI_ERR_NONE&&!stops);board_ms+=1000;done();CHECK(displays==1);
    puts("PASS pending/active/STOPPING/ACK are healthy, timer unchanged, idle ACK releases ownership");
}
static void ownership(void)
{
    setup("klq.motor_run(2,1)\nklq.wait(1)\n");state[0]=state[2]=KLQ_MOTOR_ERROR;ready[0]=ready[2]=0;step();state[1]=KLQ_MOTOR_ACTIVE;step();step();CHECK(!stops);
    state[1]=KLQ_MOTOR_ERROR;fault(2);
    setup("klq.display_number(7)\nklq.wait(1)\n");memset(ready,0,sizeof(ready));state[0]=state[1]=state[2]=KLQ_MOTOR_ERROR;step();step();step();CHECK(displays==1&&!stops);board_ms+=1000;done();
    setup("klq.motor_run(1,1)\nklq.wait(1)\n");step();mini_python_stop();CHECK(stops==1);state[0]=KLQ_MOTOR_ERROR;ready[0]=0;
    begin("klq.display_number(8)\n");done();CHECK(displays==1&&stops==2);
    setup("klq.motor_run(3,1)\n");step();klq_runtime_stop_all();state[2]=KLQ_MOTOR_ERROR;ready[2]=0;
    begin("klq.display_number(8)\n");done();CHECK(displays==1&&stops==2);
    setup("klq.motor_run(1,1)\n");step();klq_runtime_init();state[0]=KLQ_MOTOR_ERROR;ready[0]=0;
    begin("klq.display_number(8)\n");done();CHECK(displays==1);
    setup("klq.motor_run(1,1)\nklq.stop()\n");step();done();state[0]=KLQ_MOTOR_ERROR;ready[0]=0;
    begin("klq.display_number(8)\n");done();CHECK(displays==1);
    puts("PASS only owned ports monitored; display ignores old faults; stop/init/new execution clears ownership");
}
static void paired_and_stop_failure(void)
{
    setup("klq.move(0)\nklq.wait(1)\n");step();state[0]=state[1]=KLQ_MOTOR_ACTIVE;step();state[1]=KLQ_MOTOR_ERROR;fault(2);
    setup("klq.move(0)\n");ready[1]=0;CHECK(mini_python_poll()==MINI_ERROR);CHECK(mini_python_error()==MINI_ERR_UNSUPPORTED&&stops==1&&!running[0]);
    state[0]=KLQ_MOTOR_ERROR;ready[0]=0;begin("klq.display_number(8)\n");done();CHECK(displays==1);
    setup("klq.motor_run(1,1)\nklq.motor_stop(1)\nklq.wait(1)\n");step();state[0]=KLQ_MOTOR_ACTIVE;step();step();state[0]=KLQ_MOTOR_ERROR;fault(3);
    puts("PASS paired ownership/rollback and asynchronous stop failure propagation");
}
int main(void)
{waits();healthy();ownership();paired_and_stop_failure();printf("RESULT PASS assertions=%u\n",checks);return 0;}
