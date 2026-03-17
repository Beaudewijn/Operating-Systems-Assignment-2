#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>

#include "arrivals.h"
#include "intersection_time.h"
#include "input.h"

/* 
 * curr_arrivals[][][]
 *
 * A 3D array that stores the arrivals that have occurred
 * The first two indices determine the entry lane: first index is Side, second index is Direction
 * curr_arrivals[s][d] returns an array of all arrivals for the entry lane on side s for direction d,
 *   ordered in the same order as they arrived
 */
static Arrival curr_arrivals[4][3][20];

/*
 * semaphores[][]
 *
 * A 2D array that defines a semaphore for each entry lane,
 *   which are used to signal the corresponding traffic light that a car has arrived
 * The two indices determine the entry lane: first index is Side, second index is Direction
 */
static sem_t semaphores[4][3];

/*
 * supply_arrivals()
 *
 * A function for supplying arrivals to the intersection
 * This should be executed by a separate thread
 */
static void* supply_arrivals()
{
  int num_curr_arrivals[4][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}};

  // for every arrival in the list
  for (int i = 0; i < sizeof(input_arrivals)/sizeof(Arrival); i++)
  {
    // get the next arrival in the list
    Arrival arrival = input_arrivals[i];
    // wait until this arrival is supposed to arrive
    sleep_until_arrival(arrival.time);
    // store the new arrival in curr_arrivals
    curr_arrivals[arrival.side][arrival.direction][num_curr_arrivals[arrival.side][arrival.direction]] = arrival;
    num_curr_arrivals[arrival.side][arrival.direction] += 1;
    // increment the semaphore for the traffic light that the arrival is for
    sem_post(&semaphores[arrival.side][arrival.direction]);
  }

  return(0);
}

// a struct to pass attributes of side and direction to each traffic light thread
typedef struct {
  Side side;
  Direction direction;
} LightArg;

// the basic solution allows only at most one car on the intersection at all times,
// so a single mutex for the entire intersection is sufficient
static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;

/*
 * manage_light(void* arg)
 *
 * A function that implements the behaviour of a traffic light
 */
static void* manage_light(void* arg)
{
  // TODO:
  // while it is not END_TIME yet, repeatedly:
  //  - wait for an arrival using the semaphore for this traffic light
  //  - lock the right mutex(es)
  //  - make the traffic light turn green
  //  - sleep for CROSS_TIME seconds
  //  - make the traffic light turn red
  //  - unlock the right mutex(es)

  // retrieve the identifier for this process's traffic light
  LightArg* light = (LightArg*) arg;

  Side side = light->side;
  Direction direction = light->direction;

  // free the space from the struct
  free(light);

  // counter for what car at this side and direction is next
  int next_car = 0;

  while (get_time_passed() < END_TIME) {

    // wait until a car arrives at the traffic light, and if end time is reached after waiting then exit
    sem_wait(&semaphores[side][direction]);
    if (get_time_passed() >= END_TIME) {
      break;
    }

    // claim the mutex
    pthread_mutex_lock(&m);

    // retrieve the next car that arrives at this process's traffic light
    Arrival car = curr_arrivals[side][direction][next_car];
    next_car++;

    printf("traffic light %d %d turns green at time %d for car %d\n", side, direction, get_time_passed(), car.id);

    // wait for car to cross
    sleep(CROSS_TIME);

    printf("traffic light %d %d turns red at time %d\n", side, direction, get_time_passed());

    // free the mutex
    pthread_mutex_unlock(&m);
  }

  return(0);
}


int main(int argc, char * argv[])
{
  // create semaphores to wait/signal for arrivals
  for (int i = 0; i < 4; i++)
  {
    for (int j = 0; j < 3; j++)
    {
      sem_init(&semaphores[i][j], 0, 0);
    }
  }

  // start the timer
  start_time();

  // TODO: create a thread per traffic light that executes manage_light
  pthread_t light_threads[4][3];

  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 3; j++) {
      LightArg* arg = malloc(sizeof(LightArg));
      arg->side = i;
      arg->direction = j;
      
      pthread_create(&light_threads[i][j], NULL, manage_light, arg);
    }
  }

  // TODO: create a thread that executes supply_arrivals
  pthread_t supplier_thread;
  pthread_create(&supplier_thread, NULL, supply_arrivals, NULL);

  sleep(END_TIME - get_time_passed());

  // after time is over, wake up any processes waiting for cars to arrive so they can terminate
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 3; j++) {
      sem_post(&semaphores[i][j]);
    }
  }

  // TODO: wait for all threads to finish
  pthread_join(supplier_thread, NULL);

  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 3; j++) {
      pthread_join(light_threads[i][j], NULL);
    }
  }

  // destroy semaphores
  for (int i = 0; i < 4; i++)
  {
    for (int j = 0; j < 3; j++)
    {
      sem_destroy(&semaphores[i][j]);
    }
  }
}
