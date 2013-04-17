<?php

/**
 * Retrieves a list of unique queue objects registered with this module.
 *
 * This is used for clearing locks and managing queues through a UI.
 *
 * @return An array of queue items in the format:
 * - classname:
 *   - queue_name_1
 *   - queue_name_2
 *   - queue_name_3
 */
function hook_unique_queue_list() {

  // will hold the list of queues to return, keyed by classname
  $queues = array(
    'DBUniqueQueue' => array(),
  );

  // find all queue names from the DB table responsible for them
  $rs = db_select('unique_queue_queues', 'uqq')
      ->fields('uqq')
      ->execute();

  // add them to the list of queues
  foreach ($rs as $row) {
    $queues['DBUniqueQueue'][] = $row->queue_name;
  }

  // returns the list of queues
  return $queues;

}