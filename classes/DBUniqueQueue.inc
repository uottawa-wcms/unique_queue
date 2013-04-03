<?php

/**
 * An implementation of UniqueQueueInterface that uses database tables to handle
 * queue processing.
 *
 * @see UniqueQueueInterface
 */
class DBUniqueQueue extends UniqueQueue {

  /**
   * Sets the name of the queue.
   *
   * Private constructor enforces a factory pattern on queues.
   *
   * @param string $name Name of the queue
   */
  private function __construct($name) {
    parent::__construct($name);
  }

  /**
   * Frees all locks that have expired for this queue.
   */
  protected function freeLocks() {
    $q = db_update('unique_queue_items');
    $q->fields(array(
      'consumer_id' => 0,
      'lock_expires' => NULL,
    ));
    $q->condition('lock_expires', time(), '<');
    $q->condition('queue_name', $this->queue_name);
    $q->execute();
  }

  /**
   * Creates a unique token for a unique queue item.
   *
   * In this implementation, we hash the value to ensure a consistent length.
   *
   * @param string $unique_token The unique token
   * @return string A hashed value
   */
  protected function generateUniqueToken($unique_token) {
    return hash('sha384', $unique_token);
  }

  /**
   * Tries to find an item with $unique_token in the DB.
   *
   * @param string $unique_token The hashed unique token
   * @param int $priority The new priority
   * @return string One of the following values:
   * - UNIQUE_QUEUE_MISSING: If the item is not in the DB
   * - UNIQUE_QUEUE_UPDATE_REQUIRED: If the priority is too low
   * - UNIQUE_QUEUE_MATCH: If the item exists and the priority is okay
   */
  protected function seek($unique_token, $priority = 0) {
    $q = db_select('unique_queue_items', 'uqi');
    $q->fields('uqi', array(
      'item_id',
      'priority'
    ));
    $q->condition('queue_name', $this->queue_name);
    $q->condition('item_uuid', $unique_token);
    $row = $rs->execute()->fetchObject();
    if (empty($row)) {
      return UNIQUE_QUEUE_MISSING;
    } elseif ($row->priority < $priority) {
      return UNIQUE_QUEUE_UPDATE_REQUIRED;
    }
    return UNIQUE_QUEUE_MATCH;
  }

  /**
   * Adds an item to the queue.
   *
   * @param mixed $data The data we wish to store.
   * @param mixed $unique_token The unique token for the data. If NULL, the
   *   unique condition can be ignored.
   * @param int $priority A priority to store data as - items with higher values
   *   are retrieved over lower values.
   *
   * @return boolean TRUE if the item was added to the DB, otherwise FALSE.
   */
  protected function enqueue($data, $unique_token = NULL, $priority = 0) {
    if (empty($priority)) { $priority = 0; }
    $record = (object) array(
      'queue_name' => $this->queue_name,
      'data' => serialize($data),
      'item_uuid' => $unique_token,
      'created' => time(),
      'priority' => $priority,
    );
    return drupal_write_record('unique_queue_items', $record) !== FALSE;
  }

  /**
   * Updates the priority of an existing item in the queue.
   *
   * @param string $unique_token The hashed unique token.
   * @param int $priority The new priority.
   *
   * @return boolean TRUE if the item's priority was updated, otherwise FALSE.
   */
  protected function updatePriority($unique_token, $priority = 0) {
    if (empty($unique_token)) { return FALSE; }
    !empty(db_update('unique_queue_items')
      ->condition('queue_name', $this->queue_name)
      ->condition('item_uuid', $unique_token)
      ->fields(array(
        'priority' => $priority,
      ))
      ->execute());
  }

  /**
   * Retrieves the number of items left
   *
   * @return int The number of items left in the queue
   */
  public function itemsLeft($min_priority = NULL) {
    $q = db_select('unique_queue_items');
    $q->condition('queue_name', $this->queue_name);
    $q->isNotNull('consumer_id');
    if ($min_priority !== NULL && $min_priority !== FALSE) {
      $q->condition('priority', $min_priority, '>=');
    }
    return $q->countQuery()->execute()->fetchField();
  }

  /**
   * Sets up a consumer ID.
   */
  protected function initConsumerId() {
    $this->consumer_id = db_insert('unique_queue_consumers')
        ->useDefaults(array('consumer_id'))
        ->execute();
  }

  /**
   * Retrieves the next queue item that could be locked.
   *
   * @param int $min_priority The minimum priority to return. If NULL,
   *   priority will not be used as a condition.
   *
   * @return stdClass An object with data and item_id properties.
   */
  protected function peek($min_priority = NULL) {
    $q = db_select('unique_queue_items', 'uqi');
    $q->fields('uqi', array(
      'data',
      'item_id'
    ));
    $q->isNull('consumer_id');
    $q->isNull('lock_expires');
    $q->condition('queue_name', $this->queue_name);
    if ($min_priority !== NULL) {
      $q->condition('priority', $min_priority, '>=');
    }
    $q->orderBy('priority', 'DESC');
    $q->orderBy('created', 'ASC');
    // fallback sort for things created at the same time index.
    $q->orderBy('item_id', 'ASC');
    $q->range(0, 1);
    return $q->execute()->fetchObject();
  }

  /**
   * Establishes a lock on this item.
   *
   * @param stdClass $queue_object The queue object as returned by claimItem().
   * @param int $lease_time The length of time in seconds to lock the object
   *   for.
   *
   * @return boolean TRUE if the object was locked, otherwise FALSE.
   */
  protected function establishLock(&$queue_object, $lease_time = 3600) {
    $q = db_update('unique_queue_items')
    ->fields(array(
      'consumer_id' => $this->consumer_id,
      'lock_expires' => time() + $lease_time,
    ))
    ->condition('item_id', $queue_object->item_id)
    ->isNull('consumer_id');

    if ($q->execute()) {
      $queue_object->data = unserialize($item->data);
      return TRUE;
    }
    return FALSE;
  }

  /**
   * Deletes an item from the queue, after it's done being processed.
   *
   * Note that this function may return FALSE if the object is no longer in the
   * database.
   *
   * @param stdClass $item An object as returned by claimItem().
   *
   * @return boolean TRUE if the object was deleted, otherwise FALSE.
   */
  public function deleteItem($item) {
    return !!db_delete('unique_queue_items')
      ->condition('item_id', $item->item_id)
      ->execute();
  }

  /**
   * Removes all items from this queue.
   *
   * @return boolean TRUE if the objects were deleted, otherwise FALSE.
   */
  public function deleteQueue() {
    return !!db_delete('unique_queue_items')
      ->condition('queue_name', $this->queue_name)
      ->execute();
  }

}