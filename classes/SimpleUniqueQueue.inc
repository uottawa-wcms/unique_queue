<?php

class SimpleUniqueQueue {

  private $queue_name;
  private $consumer_id;

  public function __construct($name) {
    $this->queue_name = $name;
  }

  public function createItem($data, $unique_token = NULL, $priority = 0) {
    $existing = NULL;
    $existing_priority = 0;
    if ($unique_token !== NULL) {
      if ($unique_token == TRUE) {
        $unique_token = var_export($data, TRUE);
      }
      $uuid = hash('sha384', $unique_token);
      $obj = db_query('
        SELECT item_id, priority
        FROM {unique_queue_items} uqi
        WHERE uqi.item_uuid = :uuid
          AND uqi.queue_name = :name
      ', array(
        ':name' => $this->queue_name,
        ':uuid' => $uuid,
      ))->fetchObject();
      if (!empty($obj)) {
        if (!empty($obj->item_id)) {
          $existing = $obj->item_id;
          $existing_priority = $obj->priority;
        }
      }
    } else {
      $uuid = NULL;
    }
    if (!$existing) {
      $record = new stdClass();
      $record->queue_name = $this->queue_name;
      $record->data = serialize($data);
      $record->item_uuid = $uuid;
      $record->created = time();
      $record->priority = $priority;
      return drupal_write_record('unique_queue_items', $record) !== FALSE;
    } else {
      if ($existing_priority < $priority) {
        db_update('unique_queue_items')
            ->condition('queue_name', $this->queue_name)
            ->condition('item_uuid', $uuid)
            ->fields(array(
              'priority' => $priority
            ))
            ->execute();
      }
      return TRUE;
    }
  }

  public function itemsLeft() {
    return db_query('
      SELECT COUNT(item_id)
      FROM {unique_queue_items}
      WHERE queue_name = :name
    ', array(
      ':name' => $this->queue_name
    ))->fetchField();
  }

  public function claimItem($lease_time = 3600, $min_priority = 0) {
    if (empty($this->consumer_id)) {
      $this->consumer_id = db_insert('unique_queue_consumers')
          ->useDefaults(array('consumer_id'))
          ->execute();
    }
    while (TRUE) {
      $item = db_query_range('
        SELECT data, item_id
        FROM {unique_queue_items}
        WHERE consumer_id IS NULL
          AND queue_name = :name
          AND priority >= :min
          AND lock_expires IS NULL
        ORDER BY priority DESC, created ASC, item_id ASC
      ', 0, 1, array(
        ':name' => $this->queue_name,
        ':min' => $min_priority,
      ))->fetchObject();
      if ($item) {
        $update = db_update('unique_queue_items')
          ->fields(array(
            'consumer_id' => $this->consumer_id,
            'lock_expires' => time() + $lease_time,
          ))
          ->condition('item_id', $item->item_id)
          ->condition('consumer_id', NULL, 'IS');
        if ($results = $update->execute()) {
          $item->data = unserialize($item->data);
          return $item;
        }
      } else {
        return FALSE;
      }
    }
  }

  public function deleteItem($item) {
    db_delete('unique_queue_items')
      ->condition('item_id', $item->item_id)
      ->execute();
  }

  public function deleteQueue() {
    db_delete('unique_queue_items')
      ->condition('queue_name', $this->queue_name)
      ->execute();
  }

}