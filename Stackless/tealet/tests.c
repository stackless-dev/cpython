#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "tealet.h"

static int status = 0;
static tealet_t *g_main = NULL;
static tealet_t *the_stub = NULL;
static int newmode = 0;


static tealet_alloc_t talloc = TEALET_MALLOC;
void init_test() {
    assert(g_main == NULL);
    g_main = tealet_initialize(&talloc);
    assert(tealet_current(g_main) == g_main);
    status = 0;
}
void fini_test() {
    assert(g_main != NULL);
    assert(tealet_current(g_main) == g_main);
    if (the_stub)
        tealet_delete(the_stub);
    the_stub = NULL;
#ifndef NDEBUG
    assert(tealet_get_count(g_main) == 0);
#endif
    tealet_finalize(g_main);
    g_main = NULL;
}

/****************************************************************
 *Implement copyable stubs by using a trampoline
 */
struct mystub_arg
{
    tealet_t *current;
    tealet_run_t run;
    void *runarg;
};
static tealet_t *mystub_main(tealet_t *current, void *arg)
{
    void *myarg = 0;
    /* the caller is in arg, return right back to him */
    tealet_switch((tealet_t*)arg, &myarg);
    /* now we are back, myarg should contain the arg to the run function.
     * We were possibly duplicated, so can't trust the original function args.
     */
    {
        struct mystub_arg sarg = *(struct mystub_arg*)myarg;
        tealet_free(sarg.current, myarg);
        return (sarg.run)(sarg.current, sarg.runarg);
    }
}

/* create a stub and return it */
tealet_t *mystub_new(tealet_t *t) {
    void *arg = (void*)tealet_current(t);
    return tealet_new(t, mystub_main, &arg);
}

/* run a stub */
int mystub_run(tealet_t *stub, tealet_run_t run, void **parg)
{
    int result;
    void *myarg;
    /* we cannot pass arguments to a different tealet on the stack */
    struct mystub_arg *psarg = (struct mystub_arg*)tealet_malloc(stub, sizeof(struct mystub_arg));
    if (!psarg)
        return TEALET_ERR_MEM;
    psarg->current = stub;
    psarg->run = run;
    psarg->runarg = parg ? *parg : NULL;
    myarg = (void*)psarg;
    result = tealet_switch(stub, &myarg);
    if (result) {
        /* failure */
        tealet_free(stub, psarg);
        return result;
    }
    /* pass back the arg value from the switch */
    if (parg)
        *parg = myarg;
    return 0;
}



/**************************************/

/* create a tealet or stub low on the stack */
static tealet_t * tealet_new_descend(tealet_t *t, int level, tealet_run_t run, void **parg)
{
    int boo[10];
    boo[9] = 0;
    if (level > 0)
        return tealet_new_descend(t, level-1, run, parg);
    if (run)
        return tealet_new(t, run, parg);
    else
        return mystub_new(t);
}

/***************************************
 * methods for creating tealets in different ways
 */

static tealet_t * tealet_new_rnd(tealet_t* t, tealet_run_t run, void **parg)
{
       return tealet_new_descend(t, rand() % 20, run, parg);
}

static tealet_t * stub_new(tealet_t *t, tealet_run_t run, void **parg)
{
    tealet_t *stub = tealet_new_descend(t, rand() % 20, NULL, NULL);
    int res;
    if (stub == NULL)
        return NULL;
    if (run)
        res = mystub_run(stub, run, parg);
    else
        res = 0;
    if (res) {
        tealet_delete(stub);
        assert(res == TEALET_ERR_MEM);
        return NULL;
    }
    return stub;
}

static tealet_t * stub_new2(tealet_t *t, tealet_run_t run, void **parg)
{
    tealet_t *dup, *stub;
    int res;
    stub = tealet_new_descend(t, rand() % 20, NULL, NULL);
    if (stub == NULL)
        return NULL;
    dup = tealet_duplicate(stub);
    if (stub == NULL) {
        tealet_delete(stub);
        return NULL;
    }
    if (run)
        res = mystub_run(dup, run, parg);
    else
        res = 0;
    tealet_delete(stub);
    if (res) {
        tealet_delete(dup);
        assert(res == TEALET_ERR_MEM);
        return NULL;
    }
    return dup;
}

static tealet_t * stub_new3(tealet_t *t, tealet_run_t run, void **parg)
{
    tealet_t *dup;
    int res;
    if ((rand()%10) == 0)
        if (the_stub != NULL) {
            tealet_delete(the_stub);
            the_stub = NULL;
        }
    if (the_stub == NULL)
        the_stub = tealet_new_descend(t, rand() % 20, NULL, NULL);
    if (the_stub == NULL)
        return NULL;
    dup = tealet_duplicate(the_stub);
    if (dup == NULL)
        return NULL;
    if (run) {
        res = mystub_run(dup, run, parg);
        if (res) {
            tealet_delete(dup);
            assert(res == TEALET_ERR_MEM);
            return NULL;
        }
    }
    return dup;
}
    

typedef tealet_t* (*t_new)(tealet_t *, tealet_run_t, void**);
static t_new newarray[] = {tealet_new_rnd, stub_new, stub_new2, stub_new3};

static t_new get_new(){
    if (newmode >= 0)
        return newarray[newmode];
    return newarray[rand() % (sizeof(newarray)/sizeof(*newarray))];
}
#define tealet_new get_new()

/************************************************************/

void test_main_current(void)
{
  init_test();
  fini_test();
}

/************************************************************/

tealet_t *test_simple_run(tealet_t *t1, void *arg)
{
  assert(t1 != g_main);
  status = 1;
  return g_main;
}

void test_simple(void)
{
  init_test();
  tealet_new(g_main, test_simple_run, NULL);
  assert(status == 1);
  fini_test();
}

/*************************************************************/

tealet_t *test_status_run(tealet_t *t1, void *arg)
{
  assert(t1 == tealet_current(t1));
  assert(!TEALET_IS_MAIN(t1));
  assert(tealet_status(t1) == TEALET_STATUS_ACTIVE);
  return g_main;
}

void test_status(void)
{
  tealet_t *stub1;
  init_test();

  assert(tealet_status(g_main) == TEALET_STATUS_ACTIVE);
  assert(TEALET_IS_MAIN(g_main));

  stub1 = tealet_new(g_main, NULL, NULL);
  assert(tealet_status(stub1) == TEALET_STATUS_ACTIVE);
  assert(!TEALET_IS_MAIN(stub1));
  mystub_run(stub1, test_status_run, NULL);

  fini_test();
}


/*************************************************************/
tealet_t *test_exit_run(tealet_t *t1, void *arg)
{
  int result;
  assert(t1 != g_main);
  status += 1;
  result = tealet_exit(g_main, NULL, (int)arg);
  assert(0);
  return (tealet_t*)-1;
}

void test_exit(void)
{
  tealet_t *stub1, *stub2;
  int result;
  void *arg;
  init_test();
  stub1 = tealet_new(g_main, NULL, NULL);
  stub2 = tealet_duplicate(stub1);
  arg = (void*)TEALET_EXIT_NODELETE;
  result = mystub_run(stub1, test_exit_run, &arg);
  assert(result == 0);
  assert(status == 1);
  assert(tealet_status(stub1) == TEALET_STATUS_EXITED);
  tealet_delete(stub1);
  arg = (void*)TEALET_EXIT_DEFAULT;
  result = mystub_run(stub2, test_exit_run, &arg);
  assert(status == 2);
  fini_test();
}


/************************************************************/

static tealet_t *glob_t1;
static tealet_t *glob_t2;

tealet_t *test_switch_2(tealet_t *t2, void *arg)
{
  assert(t2 != g_main);
  assert(t2 != glob_t1);
  glob_t2 = t2;
  assert(status == 1);
  status = 2;
  assert(tealet_current(g_main) == t2);
  tealet_switch(glob_t1, NULL);
  assert(status == 3);
  status = 4;
  assert(tealet_current(g_main) == t2);
  tealet_switch(glob_t1, NULL);
  assert(status == 5);
  status = 6;
  assert(t2 == glob_t2);
  assert(tealet_current(g_main) == t2);
  tealet_switch(t2, NULL);
  assert(status == 6);
  status = 7;
  assert(tealet_current(g_main) == t2);
  return g_main;
}

tealet_t *test_switch_1(tealet_t *t1, void *arg)
{
  assert(t1 != g_main);
  glob_t1 = t1;
  assert(status == 0);
  status = 1;
  assert(tealet_current(g_main) == t1);
  tealet_new(g_main, test_switch_2, NULL);
  assert(status == 2);
  status = 3;
  assert(tealet_current(g_main) == t1);
  tealet_switch(glob_t2, NULL);
  assert(status == 4);
  status = 5;
  assert(tealet_current(g_main) == t1);
  return glob_t2;
}

void test_switch(void)
{
  init_test();
  tealet_new(g_main, test_switch_1, NULL);
  assert(status == 7);
  fini_test();
}

/************************************************************/

/* 1 is high on the stack.  We then create 2 lower on the stack */
/* the execution is : m 1 m 2 1 m 2 m */
tealet_t *test_switch_new_1(tealet_t *t1, void *arg)
{
    tealet_t *caller = (tealet_t*)arg;
    tealet_t *stub;
    /* switch back to the creator */
    tealet_switch(caller, NULL);
    /* now we want to trample the stack */
    stub = tealet_new_descend(t1, 50, NULL, NULL);
    tealet_delete(stub);
    /* and back to main */
    return g_main;
}

tealet_t *test_switch_new_2(tealet_t *t2, void *arg) {
    tealet_t *target = (tealet_t*)arg;
    /* switch to tealet 1 to trample the stack*/
    target->data = (void*)t2;
    tealet_switch(target, NULL);
   
    /* and then return to main */
    return g_main;
}
    
void test_switch_new(void)
{
  tealet_t *tealet1, *tealet2;
  void *arg;
  init_test();
  arg = (void *)tealet_current(g_main);
  tealet1 = tealet_new(g_main, test_switch_new_1, &arg);
  /* the tealet is now running */
  arg = (void*)tealet1;
  tealet2 = tealet_new_descend(g_main, 4, test_switch_new_2, &arg);
  assert(tealet_status(tealet2) == TEALET_STATUS_ACTIVE);
  tealet_switch(tealet2, NULL);
  /* tealet should be dead now */
  fini_test();
}

/************************************************************/

/* test argument passing with switch and exit */
tealet_t *test_arg_1(tealet_t *t1, void *arg)
{
  void *myarg;
  tealet_t *peer = (tealet_t*)arg;
  myarg = (void*)1;
  tealet_switch(peer, &myarg);
  assert(myarg == (void*)2);
  myarg = (void*)3;
  tealet_exit(peer, myarg, TEALET_EXIT_DEFAULT);
  return NULL;
}

void test_arg(void)
{
  void *myarg;
  tealet_t *t1;
  init_test();
  myarg = (void*)g_main;
  t1 = tealet_new(g_main, test_arg_1, &myarg);
  assert(myarg == (void*)1);
  myarg = (void*)2;
  tealet_switch(t1, &myarg);
  assert(myarg == (void*)3);
  fini_test();
}

/************************************************************/

#define ARRAYSIZE  127
#define MAX_STATUS 50000

static tealet_t *tealetarray[ARRAYSIZE] = {NULL};
static int got_index;

tealet_t *random_new_tealet(tealet_t*, void *arg);

static void random_run(int index)
{
  int i, prevstatus;
  void *arg;
  tealet_t *cur = tealet_current(g_main);
  assert(tealetarray[index] == cur);
  do
    {
      i = rand() % (ARRAYSIZE + 1);
      status += 1;
      if (i == ARRAYSIZE)
        break;
      prevstatus = status;
      got_index = i;
      if (tealetarray[i] == NULL)
        {
          if (status >= MAX_STATUS)
            break;
          arg = (void*)i;
          tealet_new(g_main, random_new_tealet, &arg);
        }
      else
        {
          tealet_switch(tealetarray[i], NULL);
        }
      assert(status >= prevstatus);
      assert(tealet_current(g_main) == cur);
      assert(tealetarray[index] == cur);
      assert(got_index == index);
    }
  while (status < MAX_STATUS);
}

tealet_t *random_new_tealet(tealet_t* cur, void *arg)
{
  int i = got_index;
  assert(tealet_current(g_main) == cur);
  assert(i == (int)(arg));
  assert(i > 0 && i < ARRAYSIZE);
  assert(tealetarray[i] == NULL);
  tealetarray[i] = cur;
  random_run(i);
  tealetarray[i] = NULL;

  i = rand() % ARRAYSIZE;
  if (tealetarray[i] == NULL)
    {
      assert(tealetarray[0] != NULL);
      i = 0;
    }
  got_index = i;
  return tealetarray[i];
}

void test_random(void)
{
  int i;
  init_test();
  for( i=0; i<ARRAYSIZE; i++)
      tealetarray[i] = NULL;
  tealetarray[0] = g_main;
  status = 0;
  while (status < MAX_STATUS)
    random_run(0);

  assert(g_main == tealetarray[0]);
  for (i=1; i<ARRAYSIZE; i++)
    while (tealetarray[i] != NULL)
      random_run(0);

  fini_test();
}



/************************************************************/

/* Another random switching test.  Tealets find a random target
 * tealet to switch to and do that.  While the test is running, they
 * generate new tealets to fill the array.  Each tealet runs x times
 * and then exits.  The switching happens at a random depth.
 */
#define N_RUNS 10
#define MAX_DESCEND 20

void random2_run(int index);
tealet_t *random2_tealet(tealet_t* cur, void *arg)
{
  int index = (int)arg;
  assert(tealet_current(g_main) == cur);
  assert(index > 0 && index < ARRAYSIZE);
  assert(tealetarray[index] == NULL);
  tealetarray[index] = cur;
  random2_run(index);
  tealetarray[index] = NULL;
  return tealetarray[0]; /* switch to main */
}
void random2_new(int index) {
    void *arg = (void*)index;
    tealet_new(g_main, random2_tealet, &arg);
}

int random2_descend(int index, int level) {
    int target;
    if (level > 0)
        return random2_descend(index, level-1);
  
    /* find target */
    target = rand() % ARRAYSIZE;
    if (status<MAX_STATUS) {
        status += 1;
        while(target == index)
            target = rand() % ARRAYSIZE;
        if (tealetarray[target] == NULL)
            random2_new(target);
        else
            tealet_switch(tealetarray[target], NULL);
        return 1;
    } else {
        /* find a telet */
        int j;
        for (j=0; j<ARRAYSIZE; j++) {
            int k = (j + target) % ARRAYSIZE;
            if (k != index && tealetarray[k]) {
                status += 1;
                tealet_switch(tealetarray[k], NULL);
                return 1;
            }
        }
        return 0;
    }
}
            

void random2_run(int index)
{
    int i;
    assert(tealetarray[index] == NULL || tealetarray[index] == tealet_current(g_main));
    tealetarray[index] = tealet_current(g_main);
    for (i=0; i<N_RUNS; i++)
        if (random2_descend(index, rand() % (MAX_DESCEND+1)) == 0)
            break;
    tealetarray[index] = NULL;
}


void test_random2(void)
{
    int i;
    init_test();
    for( i=0; i<ARRAYSIZE; i++)
        tealetarray[i] = NULL;
    tealetarray[0] = g_main;
 
    while (status < MAX_STATUS)
        random2_run(0);

    /* drain the system */
    tealetarray[0] = tealet_current(g_main);
    for(;;) {
        for(i=1; i<ARRAYSIZE; i++)
            if (tealetarray[i]) {
                status++;
                tealet_switch(tealetarray[i], NULL);
                break;
            }
        if (i == ARRAYSIZE)
            break;
    }
    tealetarray[0] = NULL;
    fini_test();
}
static void (*test_list[])(void) = {
  test_main_current,
  test_simple,
  test_status,
  test_exit,
  test_switch,
  test_switch_new,
  test_arg,
  test_random,
  test_random2,
  NULL
};


void runmode(int mode)
{
    int i;
    newmode = mode;
    printf("+++ Running tests with newmode = %d\n", newmode);
    for (i = 0; test_list[i] != NULL; i++)
    {
        printf("+++ Running test %d... +++\n", i);
        test_list[i]();
    }
    printf("+++ All ok. +++\n");
}

int main(int argc, char **argv)
{
    int i;
    for (i = 0; i<3; i++)
        runmode(i);
    runmode(-1);
    return 0;
}
