<?php

/**
 * Page callback for listing all unique queues.
 */
function unique_queue_ui_list() {
  $queues = UniqueQueue::getAllQueues();
  $rows = array();
  $header = array(
    'name' => t('Queue Name'),
    'type' => t('Class'),
    'items' => t('Items'),
  );
  foreach ($queues as $classname => $queue_list) {
    foreach ($queue_list as $queue_name) {
      $queue = UniqueQueue::get($queue_name, $classname);
      $rows[] = array(
        'name' => l($queue_name, 'admin/config/system/unique_queue/' . $queue_name),
        'type' => $classname,
        'items' => $queue->numberOfItems(),
      );
    }
  }
  return theme('table', array(
    'rows' => $rows,
    'header' => $header,
    'empty' => t('There are no unique queues registered'),
  ));
}

/**
 * Form callback for viewing a list of queue items and performing bulk operations
 * on them.
 */
function unique_queue_ui_queue_form($form, &$form_state, $queue) {
  $header = array(
    'id' => t('ID'),
    'priority' => t('Priority'),
    'status' => t('Status'),
    'created' => t('Created'),
    'lock_expires' => t('Lock Expires'),
  );
  $form['action'] = array(
    '#title' => t('Action'),
    '#type' => 'select',
    '#options' => array(
      'unlock' => t('Unlock Items'),
      'delete' => t('Delete Items'),
    ),
    '#empty_option' => t('None'),
    '#empty_value' => '',
    '#default_value' => '',
  );
  $rows = array();
  $queue = UniqueQueue::get($queue['queue_name'], $queue['classname']);
  $items = $queue->listItems();
  foreach ($items as $item) {
    $rows[$item->item_uuid] = array(
      'id' => l($item->item_id, 'admin/config/system/unique_queue/' . $queue->queueName() . '/' . $item->item_uuid),
      'priority' => $item->priority,
      'status' => empty($item->lock_expires) ? t('Queued') : t('Locked'),
      'created' => format_date($item->created, 'long'),
      'lock_expires' => empty($item->lock_expires) ? t('N/A') : format_date($item->lock_expires, 'long')
    );
  }
  $form_state['queue'] = $queue;
  $form['queue_items'] = array(
    '#type' => 'tableselect',
    '#header' => $header,
    '#options' => $rows,
  );
  $form['submit'] = array(
    '#type' => 'submit',
    '#value' => t('Execute'),
  );
  return $form;
}

/**
 * Submit handler for unique_queue_ui_queue_form().
 */
function unique_queue_ui_queue_form_submit($form, &$form_state) {
  if (!empty($form_state['values']['action'])) {
    $queue = $form_state['queue'];
    foreach ($form_state['values']['queue_items'] as $item_id => $selected) {
      if (!empty($selected)) {
        $item = $queue->itemPeek($item_id);
        if (!empty($item)) {
          switch ($form_state['values']['action']) {
            case 'unlock':
              $queue->releaseItem($item);
              drupal_set_message(t('Released item %id from queue %queue', array(
                '%queue' => $queue->queueName(),
                '%id' => $item->item_id
              )));
              break;
            case 'delete':
              $queue->deleteItem($item);
              drupal_set_message(t('Deleted item %id from queue %queue', array(
                '%queue' => $queue->queueName(),
                '%id' => $item->item_id
              )));
              break;
          }
        }
      }
    }
  }
}

/**
 * Page callback for viewing a queue item.
 */
function unique_queue_view_item($item, $queue) {
  return theme('table', array(
    'header' => array(
      'header' => '',
      'info' => 'Value',
    ),
    'rows' => array(
      array(
        'header' => '<strong>' . t('Queue ID') . '</strong>',
        'info' => $item->item_id,
      ),
      array(
        'header' => '<strong>' . t('Queue Name') . '</strong>',
        'info' => $item->queue_name,
      ),
      array(
        'header' => '<strong>' . t('Unique ID') . '</strong>',
        'info' => $item->item_uuid,
      ),
      array(
        'header' => '<strong>' . t('Priority') . '</strong>',
        'info' => $item->priority,
      ),
      array(
        'header' => '<strong>' . t('Status') . '</strong>',
        'info' => empty($item->lock_expires) ? t('Queued') : t('Locked'),
      ),
      array(
        'header' => '<strong>' . t('Created') . '</strong>',
        'info' => format_date($item->created, 'long'),
      ),
      array(
        'header' => '<strong>' . t('Lock Expires') . '</strong>',
        'info' => empty($item->lock_expires) ? t('N/A') : format_date($item->lock_expires, 'long'),
      ),
      array(
        'header' => '<strong>' . t('Information') . '</strong>',
        'info' => '<pre>' . var_export(unserialize($item->data), TRUE) . '</pre>',
      ),
    ),
  ));
  return var_export($item, TRUE);
}