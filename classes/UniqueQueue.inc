<?php

/**
 * Indicates the the unique ID was not found within the queue
 */
define('UNIQUE_QUEUE_MISSING', 'missing');

/**
 * Indicates that the unique ID was found but the priority was lower than
 * was expected.
 */
define('UNIQUE_QUEUE_UPDATE_REQUIRED', 'update');

/**
 * Indicates that the unique ID was found within the queue and that the priority
 * was as expected.
 */
define('UNIQUE_QUEUE_MATCH', 'match');

/**
 * Provides a common interface for unique queues to follow.
 *
 * This queue structure allows for the following features:
 * - Standard queues (no uniquness constraints, no prioritization)
 * - Unique Queues that use a unique token to prevent duplicate items from
 *   entering the queue.
 * - Priority Queues that allow for a prioritization of items within them
 * - Queues that combine Unique and Priority features.
 *
 * Unique queues are useful to avoid having the same item added multiple times.
 * This can help avoid duplicate processing and conflict errors.
 *
 * Priority queues are useful if you want to prioritize some objects in the
 * queue over others, or if you want to only process some items during high
 * usage periods. For example, if some items must be processed ASAP and some
 * can be delayed until the evening's cron job, then you can use priorities to
 * differentiate between the two and simply change the priority that claimItem
 * uses based on the current time.
 *
 * All four types of queues use the same four functions with overloaded
 * parameters:
 *
 * createItem():
 * - Call createItem($data) to create an non-prioritized, non-unique item.
 * - Call createItem($data, TRUE) to create a non-prioritized but unique item.
 *   The unique token will be based on the serialized value of $data.
 * - Call createItem($data, $unique_key) to specify your own unique token.
 * - Call createItem($data, NULL, $priority) with an integer priority to create
 *   a prioritized but non-unique item. Higher priority items are processed
 *   first. The default priority is 0 if you don't pass a priority for some
 *   items.
 * - Call createItem($data, TRUE, $priority) or createItem($data, $unique_key,
 *   $priority) to create a unique and prioritized queue entry.
 *
 * claimItem($lease_time = 3600):
 * - Call claimItem() (optionally with a $lease_time value) to simply claim the
 *   next item based on priority and order they were entered into the queue. It
 *   will return an array for the item with the oldest item that has the highest
 *   priority of those currently in the queue.
 * - Call claimItem(3600, $min_priority) to claim the next item but with a
 *   minimum priority. Items with a lower priority will NOT be returned.
 *
 * deleteItem($object):
 * - No matter what parameters were used, call deleteItem with the object from
 *   claimItem() when you're done with it.
 *
 * deleteQueue():
 * - You can always call this on any queue to erase all the items from it.
 *
 */
abstract class UniqueQueue implements DrupalQueueInterface {

  /**
   * Create a new instance of a queue.
   *
   * We use the factory method pattern to abstract out which type of queue is
   * created. The default class is DBUniqueQueue. You can override this setting
   * using the unique_queue_default_implementation variable. You can also
   * override it on a case-by-case basis using the second parameter of this
   * function.
   *
   * We also enforce a multiton (or registry of singletons) pattern using
   * drupal_static which ensures there is only ever one instance of any given
   * queue per page call. This allows us to save on consumer IDs to avoid
   * bloating the data storage system with them.
   *
   * The UniqueQueue implementation includes support for:
   * - Queues
   * - Priority Queues
   * - Unique Queues
   * - hybrid Priority / Unique Queues
   *
   * @param string $queue_name The name of the queue you want to work with. New
   *   queues do not need any special treatment.
   * @param class $type The name of a class to instanstiate. Should extend
   *   UniqueQueue. If NULL or invalid, we will fallback on the defaults.
   *
   * @return \UniqueQueue An instance of a subclass of UniqueQueue, capable of
   *   handling all tasks related to queue processing.
   */
  public static function get($queue_name, $type = NULL) {
    $queues = drupal_static('unique_queue_queues', array());
    if (!isset($queues[$queue_name])) {
      // make sure the provided $type is a valid class name for a unique queue.
      if (empty($type) || (!class_exists($type)) || (!is_subclass_of($type, 'UniqueQueue'))) {
        $type = variable_get('unique_queue_default_implementation', 'DBUniqueQueue');
        // make sure the default variable is a valid class name for a unique
        // queue.
        if (!class_exists($type) || (!is_subclass_of($type, 'UniqueQueue'))) {
          // fallback to using a DB-based queue
          $type = 'DBUniqueQueue';
        }
      }
      $queues[$queue_name] = new $type($queue_name);
    }
    return $queues[$queue_name];
  }

  /**
   * Cleans up the locks on queues. The option exists to only trigger on some
   * of the queues.
   *
   * @param array $queue_names An array of queue names to operate on. If empty,
   *   all queues will have their locks freed.
   *
   */
  public static function triggerFreeLocks($queue_names = NULL) {
    $queues = UniqueQueue::getAllQueues();
    foreach ($queues as $classname => $queue_list) {
      foreach ($queue_list as $queue_name) {
        if (empty($queue_names) || in_array($queue_name, $queue_names)) {
          $queue = UniqueQueue::get($queue_name, $classname);
          $queue->freeLocks();
        }
      }
    }
  }

  /**
   * Retrieves a list of all queues that are registered using registerQueue().
   * These queues are used to free up locks and to provide UI reports.
   *
   * Implementations can add their queues to this list using
   * hook_unique_queue_list().
   *
   * @return array An array of queue names, in the following format:
   * - classname:
   *   - queue_name_1
   *   - queue_name_2
   *   - queue_name_3
   */
  public static function getAllQueues() {
    $queues = drupal_static('unique_queue_list', array());
    if (empty($queues)) {
      $modules = module_implements('unique_queue_list');
      foreach ($modules as $module) {
        $hook = $module . '_unique_queue_list';
        $results = $hook();
        foreach ($results as $classname => $list_of_queues) {
          if (empty($queues[$classname])) {
            $queues[$classname] = $list_of_queues;
          } else {
            $queues[$classname] = array_unique(array_merge($queues[$classname], $list_of_queues));
          }
        }
      }
    }
    return $queues;
  }

  /**
   * The name of the queue. Each queue must provide a unique one.
   * @var string
   */
  protected $queue_name;

  /**
   * The unique identifer of the consumer.
   *
   * Should be of a type that is appropriate for the data storage method.
   *
   * @var mixed
   */
  protected $consumer_id;

  /**
   * Initializes the queue object by settings the name of the queue.
   *
   * @param string $name The unique name of the queue.
   */
  public function __construct($name) {
    $this->queue_name = $name;
    $this->registerQueue();
  }

  /**
   * Retrieves the queue name.
   *
   * We hide this behind a function so that the queue name cannot be externally
   * manipulated.
   *
   * @return string The name of this queue.
   */
  public function queueName() {
    return $this->queue_name;
  }

  /**
   * Registers the queue name with the data storage method so that we can find
   * all the queues later.
   *
   * Data storage implementations should implement hook_unique_queue_list() to
   * return a list of all queues registered using this function.
   */
  abstract protected function registerQueue();

  /**
   * Creates an item within the queue.
   *
   * The unique token is generated by buildUniqueToken() and then passed to
   * seek() to see what we need to do with the item.
   * - If the item is not present in the queue, enqueue() will be called.
   * - If the item is present but the priority is too low, updatePriority() will
   *     be called.
   * - Otherwise, we will just return TRUE.
   *
   * @param mixed $data The data that we want to store in the queue.
   * @param mixed $unique_token If empty, uniqueness will not be used
   *   for this queue. If TRUE, a unique ID will be generated by serializing
   *   $data. If set to a non-empty, non-boolean value, it will be used as the
   *   unique ID.
   * @param int $priority A priority value for the queue item. Higher values
   *   will be given priority over lower values.
   *
   * @return boolean TRUE if the value is present in the queue or was added,
   *   otherwise FALSE.
   *
   * @see UniqueQueueInterface::buildUniqueToken()
   * @see UniqueQueueInterface::seek()
   * @see UniqueQueueInterface::enqueue()
   * @see UniqueQueueInterface::updatePriority()
   */
  public function createItem($data, $unique_token = NULL, $priority = 0) {
    $unique_token = $this->buildUniqueToken($data, $unique_token);
    // fast return MISSING for null unique items to avoid having to call seek()
    // pointlessly.
    if (empty($unique_token)) { return UNIQUE_QUEUE_MISSING; }
    $status = $this->seek($unique_token, $priority);
    switch ($status) {
      case UNIQUE_QUEUE_MISSING:
        return $this->enqueue($data, $unique_token, $priority);
        break;
      case UNIQUE_QUEUE_UPDATE_REQUIRED:
        return $this->updatePriority($data, $unique_token, $priority);
        break;
      case UNIQUE_QUEUE_MATCH:
      default:
        // do nothing - fall through to the return true below
    }
    return TRUE;
  }

  /**
   * Claims an item from the queue.
   *
   * Starts by claiming a consumer ID if none exist, then finds the next
   * unlocked item in the queue and attempts to lock it. If that locking is
   * successful, the item is returned as an object.
   *
   * If no item can be locked or there are no items in the queue, NULL is
   * returned.
   *
   * The default lease time is taking from the variable
   * unique_queue_default_lease_time and defaults to 3600 seconds on install.
   *
   * @param int $lease_time The number of seconds we will lock the item for.
   *   This should be long enough for the consumer to finish processing it.
   * @param int|NULL $min_priority Only items with a priority equal to or greater
   *   than this value will be returned. If NULL, priority will not be used as
   *   a factor.
   *
   * @return stdClass An object representing an item in the queue.
   *
   * @see UniqueQueueInterface::initConsumerId()
   * @see UniqueQueueInterface::peek()
   * @see UniqueQueueInterface::establishLock()
   */
  public function claimItem($lease_time = NULL, $min_priority = NULL) {
    if (empty($lease_time)) {
      $lease_time = variable_get('unique_queue_default_lease_time', 3600);
    }
    if (empty($this->consumer_id)) {
      $this->initConsumerId();
    }
    while (TRUE) {
      $next_object = $this->peek($min_priority);
      if (empty($next_object)) {
        return NULL;
      }
      if ($this->establishLock($next_object, $lease_time)) {
        return $next_object;
      }
    }
  }

  /**
   * Generates a unique token based on the unique_token value passed and, if
   * needed, the data passed as well.
   *
   * The token is generated as follows:
   * - If $unique_token is empty (NULL), then we return NULL - no unique value
   *   should be used for this item.
   * - If $unique_token is TRUE, then $data is serialized and used in the next
   *   step instead of $unique_token.
   * - Otherwise, $unique_token (or serialized $data) is passed to
   *   generateUniqueToken() for the queue implementor to serialize the value.
  *
   * @param mixed $data The data that we want to store in the queue.
   * @param mixed $unique_token The unique token to use, or TRUE to use $data or
   * NULL to not use a unique value.
   *
   * @return mixed A unique token to be used for this item or NULL if there is
   * no unique token.
   */
  private function buildUniqueToken($data, $unique_token = NULL) {
    if (empty($unique_token)) {
      return NULL;
    }
    elseif ($unique_token === TRUE) {
      $unique_token = serialize($data);
    }
    return $this->generateUniqueToken($unique_token);
  }

  /**
   * Returns the number of items left in the queue.
   *
   * Added to support DrupalQueueInterface.
   */
  public function numberOfItems() {
    return $this->itemsLeft();
  }

  /**
   * Returns the number of items left in the queue.
   *
   * @deprecated In favour of using numberOfItems().
   * @todo Remove this method once I've moved everything over to numberOfItems().
   *
   * @param int|NULL $min_priority The minimum priority to check for objects. If
   *   NULL, priority will not be used as a factor.
   *
   * @return int The number of items left in the queue.
   */
  public abstract function itemsLeft($min_priority = NULL);

  /**
   * Deletes an item from the queue.
   *
   * Implementing functions can assume this item has been completed and is
   * ready to be dequeued.
   *
   * @param stdClass $queue_object The queue object as returned by claimItem().
   *
   * @return boolean TRUE if the item is removed, otherwise FALSE.
   */
  public abstract function deleteItem($queue_object);

  /**
   * Deletes all the items from the queue.
   *
   * Implementing functions should wipe all items from the queue.
   *
   * @return boolean TRUE if all items were removed, otherwise FALSE.
   */
  public abstract function deleteQueue();

  /**
   * Creates a valid unique token for the queue based on a string.
   *
   * @param string $unique_token The unique token as determined by
   *   buildUniqueToken().
   *
   * @return mixed A valid unique token for this queue to use.
   *
   * @see UniqueQueueInterface::buildUniqueToken().
   */
  protected abstract function generateUniqueToken($unique_token);

  /**
   * Attempts to lock a queue object.
   *
   * Implementing functions should attempt to lock an item in a fashion that
   * correctly deals with concurrent access using consumer ID. They should also
   * be prepared to free all expired locks when freeLocks() is called. This
   * means generally that the expiry time must be stored.
   *
   * @param array $queue_object A queue object as returned by seek().
   * @param int $lease_time How long to lock the object for
   *
   * @return TRUE if the object was successfully locked, otherwise FALSE.
   */
  protected abstract function establishLock(&$queue_object, $lease_time = 3600);

  /**
   * Frees all locks that have expired.
   */
  protected abstract function freeLocks();

  /**
   * Sets up a consumer ID. Each consumer ID should be unique.
   *
   * Implementing functions should simply set the protected $consumer_id
   * variable to a unique value. They can assume that $consumer_id is empty.
   */
  protected abstract function initConsumerId();

  /**
   * Retrieves the next queue item that could be locked potentially.
   *
   * Implementing functions should filter and order the results as follows:
   * - Any item with a priority below $min_priority should be excluded
   * - Items should be ordered by priority (highest value first), and then by
   *   the time added to the queue (oldest item first).
   *
   * The return value should be an object, with at least
   * these two properties:
   * - data: The data passed to createItem() originally.
   * - item_id: A unique ID that represents the item within this queue.
   *
   * @param int $min_priority The minimum priority value for the returned item.
   *   If NULL, priority will not be used as factor.
   *
   * @return stdClass An object with data and item_id properties of the oldest item
   *   of the highest priority in the queue. If $min_priority is not null, then
   *   the priority will be of at least that value.
   */
  protected abstract function peek($min_priority = NULL);

  /**
   * Retrieves a list of all items currently in the queue, without locking them.
   *
   * Methods that use this to get queue items should NOT process the queue
   * items. This function exists for informational purposes only!
   *
   * @return array An interable of queue objects.
   */
  public abstract function listItems();

  /**
   * Retrieves details on an item without locking it.
   *
   * Methods that use this should NOT process the queue item. This function
   * exists for informational purposes only!
   *
   * @param string $uuid The UUID to lookup
   *
   * @return stdClass An object with all the details of a queue object:
   * - item_uuid: The UUID of the item
   * - priority: The numeric priority of the item
   * - consumer_id: The current owner of the item, or empty if unowned
   * - data: The unserialized data of the item
   * - lock_expires: The time that the lock expires or empty if unlocked
   * - created: The time the object was created
   */
  public abstract function itemPeek($uuid);

  /**
   * Checks to see if $unique_token exists in the queue already with at least
   * the given priority.
   *
   * Implementing function can assume that if $unique_token is empty, the item
   * is not in the queue. This should not happen very often though.
   *
   * @param mixed $unique_token A unique token, as returned by
   *   generateUniqueToken().
   * @param int $priority The priority we would insert the data as.
   *
   * @return string A string constant based on the following:
   *   - UNIQUE_QUEUE_MISSING: If the item is not present in the queue.
   *   - UNIQUE_QUEUE_UPDATE_REQUIRED: If the item is present in the queue, but
   *     the current item in the queue has a lower priority than the new item.
   *   - UNIQUE_QUEUE_MATCH: If the item is in the queue and the priority is
   *     the same or greater than the new item.
   */
  protected abstract function seek($unique_token, $priority = 0);

  /**
   * Adds an item to the queue.
   *
   * Implementing functions should ensure only one $data exists for any given
   * $unique_token at any given time in the queue. If $unique_token is empty,
   * this restriction can be ignored.
   *
   * Implementing functions should also add a timestamp or other method of
   * ordering to ensure that items can be claimed in the order they were added.
   *
   * @param mixed $data The data we wish to store.
   * @param mixed $unique_token The unique token for the data. If NULL, the
   *   unique condition can be ignored.
   * @param int $priority A priority to store data as - items with higher values
   *   are retrieved over lower values.
   *
   * @return boolean TRUE if the item was added to the queue, otherwise FALSE.
   */
  protected abstract function enqueue($data, $unique_token = NULL, $priority = 0);

  /**
   * Updates the priority of an item in the queue.
   *
   * Implementing functions should only update the priority value for the item
   * with the unique token.
   *
   * @param mixed $unique_token The unique token of an existing piece of data.
   * @param int $priority The priority value to update the priority to.
   *
   * @return boolean TRUE if the item was updated, otherwise FALSE.
   */
  protected abstract function updatePriority($unique_token, $priority = 0);

}