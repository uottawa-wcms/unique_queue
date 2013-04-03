This module provides an extended API for creating and managing queues. It is
designed to be used as an API for other modules and has no user-facing
components at this time.

Additional queue features currently provided include:
- Uniqueness constraint on items being added to the queue
- Priority ordering for items being popped out of the queue

In all places, we've attempted to emulate the core Queue implementation as
closely as possible, to minimize the amount of developer wtf.


### Getting a queue object

To use our queue module, first you will need to get an instance of a queue
object. This is as simple as calling:

$queue = UniqueQueue::get($queue_name);

$queue_name is a unique string identifier for your queue. No additional code is
necessary for new or existing queues. The call uses the multiton pattern, so you
will only get one queue object per $queue_name within a single request.


### Adding an item to the queue

With your queue object, you can easily add an item to the queue:

    $queue->createItem($data);

$data can be anything you want, as long as it is serializable. There are also
two other optional parameters:

- The $unique_token parameter can be set to TRUE or a string. If it's TRUE, a
  unique token for the queue item will be created based on the serialization of
  $data. If it's otherwise not empty, whatever you pass will be used for the
  unique token. We recommend you use a string token where possible to avoid
  having to serialize the data twice. This is especially important if $data is
  a large or complex data structure. Setting the value to NULL (the default)
  means that the item is not queue and will be ignored when checking for
  uniqueness.
- The $priority parameter can be set to an integer (default is 0). Items will be
  returned in order of priority first - items of a similar priority will be
  returned in the order they were added to the queue. Priority is completely up
  to implementing applications to set, as long as the values are numeric and
  higher values indicate more important objects.

Example: Adding a unique item, using the serialization of $data:
    $queue->createItem($data, TRUE);

Example: Adding a unique item, with a custom unique key:
    $key = 'node--' . $data->nid; // let's say $data is a node object
    $queue->createItem($data, $key);

Example: Adding a high priority item, without using the unique feature:
    $queue->createItem($data, NULL, 100);

Example: Adding a high priority item, with a serialized unique key:
    $queue->createItem($data, TRUE, 100);


### Processing items from a queue

To process items from a queue, you can use the following to retrieve them:

    $queue_object = $queue->claimItem();

This will return an object. The $data you passed to createItem is stored in
$queue_object->data. When you are done with the object, you should call the
following to remove the item from the queue (otherwise it will remain in queue
and locked):

    // do something with $queue_object->data;
    $queue->deleteItem($queue_object);

claimItem() has two optional parameters:

- $lease_time allows you to control how long the object stays locked for. If
  processing is interrupted before you deleteItem, the object will stay locked
  for at least this many seconds (and will be unlocked during the next cron).
  You should set this to a reasonable value based on how long you expect to
  spend processing one item. The default is one hour (3600 seconds). The value
  is in seconds.
- $min_priority allows you to only claim items of a certain priority. Lower
  items are invisible (but still queued) if you specify this value. Calling
  this method without a $min_priority will return items of any priority (but
  still sorted in priority order).

Example: Only lock items for two minutes:
    $queue_object = $queue->claimItem(180);

Example: Only retrieve high priority ($priority >= 100) items, use default
lease time.
    $queue_object = $queue->claimItem(NULL, 100);


### Removing all items from a queue

If you ever need to completely empty a queue, you can use the following:

  $queue->deleteQueue();

There are no parameters for deleteQueue().

### Creating your own queue implementation

The default implementation is DB-driven (DBUniqueQueue). You can add your own
implementations simply by creating a class that extends UniqueQueue. To make
yours the default, you can play with variable configuration settings.

