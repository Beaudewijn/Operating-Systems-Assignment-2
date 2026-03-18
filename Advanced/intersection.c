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

// nine mutexes are needed to ensure there are no collisions in the crossing
// these are for the following possible collisions and in the array indexed in the following order:
// 0: north exit: east right - west left - south straight
// 1: west exit: east straight - north right - south left
// 2: south exit: east left - north straight - west right
// 3: east straight - north straight - south left
// 4: east straight - south straight - west left
// 5: east left - south straight
// 6: east left - south left
// 7: north straight - west left
// 8: west left - south left
#define NUM_MUTEXES 9
static pthread_mutex_t conflicts[NUM_MUTEXES];

// create an array that for each traffic light stores how many mutexes it needs to obtain
int light_mutexes_count[4][3] = {{0, 3, 1}, {3, 3, 1}, {4, 3, 0}, {4, 0, 1}};

// create an array that for each traffic light stores what mutexes it needs to obtain
int light_mutexes[4][3][NUM_MUTEXES] = {
  [NORTH] = {
    [LEFT] = {}, 
    [STRAIGHT] = {2, 3, 7}, 
    [RIGHT] = {1}
  }, 
  [EAST] = {
    [LEFT] = {2, 5, 6}, 
    [STRAIGHT] = {1, 3, 4}, 
    [RIGHT] = {0}
  }, 
  [SOUTH] = {
    [LEFT] = {1, 3, 6, 8}, 
    [STRAIGHT] = {0, 4, 5}, 
    [RIGHT] = {}
  }, 
  [WEST] = {
    [LEFT] = {0, 4, 7, 8}, 
    [STRAIGHT] = {}, 
    [RIGHT] = {2}
  }};

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

    // claim all the mutexes it needs to claim to ensure no intersecting cars are currently on the crossing
    for (int i = 0; i < light_mutexes_count[side][direction]; i++) {
      pthread_mutex_lock(&conflicts[light_mutexes[side][direction][i]]);
    }

    // retrieve the next car that arrives at this process's traffic light
    Arrival car = curr_arrivals[side][direction][next_car];
    next_car++;

    printf("traffic light %d %d turns green at time %d for car %d\n", side, direction, get_time_passed(), car.id);

    // wait for car to cross
    sleep(CROSS_TIME);

    printf("traffic light %d %d turns red at time %d\n", side, direction, get_time_passed());

    // free the mutexes
    for (int i = 0; i < light_mutexes_count[side][direction]; i++) {
      pthread_mutex_unlock(&conflicts[light_mutexes[side][direction][i]]);
    }
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

  // create mutexes to avoid collisions
  for (int i = 0; i < sizeof(conflicts)/sizeof(conflicts[0]); i++) {
    pthread_mutex_init(&conflicts[i], NULL);
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
