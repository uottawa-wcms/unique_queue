<?php

class UniqueQueue {

  public static function get($queue_name) {
    static $queues;
    if (!isset($queues[$queue_name])) {
      $queues[$queue_name] = new SimpleUniqueQueue($queue_name);
    }
    return $queues[$queue_name];
  }

}