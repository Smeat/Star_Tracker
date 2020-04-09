/*
 * queue.h 
 * By Steven de Salas
 */

#ifndef __QUEUE_H
#define __QUEUE_H


template<class T>
class queue {
  private:
	  QueueHandle_t _queue;
  public:
    queue(int maxitems = 256) { 
		_queue = xQueueCreate(maxitems, sizeof(T));
    }
    void push(const T &item);
	int count();
    T pop();
	T peek();
    void clear();
};

template<class T>
void queue<T>::push(const T &item)
{
	xQueueSend(_queue, &item, portMAX_DELAY);
}

template<class T>
int queue<T>::count()
{
	return uxQueueMessagesWaiting(_queue);
}

template<class T>
T queue<T>::pop() {
	T obj;
	xQueueReceive(_queue, &obj, portMAX_DELAY);
	return obj;
}

template<class T>
T queue<T>::peek() {
	T obj;
	xQueuePeek(_queue, &obj, portMAX_DELAY);
	return obj;
}

template<class T>
void queue<T>::clear() 
{
	xQueueReset(_queue);
}

#endif
