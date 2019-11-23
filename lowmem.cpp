#include <iostream>
#include <cstdint>
#include <cstring>
#include <ctime>

#define KB * 1024
#define MB * 1024 * 1024
#define GB * 1024 * 1024 * 1024

class timer { public: std::chrono::time_point<std::chrono::high_resolution_clock> lastTime; timer() : lastTime(std::chrono::high_resolution_clock::now()) {} inline double elapsed() { std::chrono::time_point<std::chrono::high_resolution_clock> thisTime=std::chrono::high_resolution_clock::now(); double deltaTime = std::chrono::duration<double>(thisTime-lastTime).count(); lastTime = thisTime; return deltaTime; } };

namespace mmngr
{
	const uint64_t gc_mask = 1;
	const uint64_t black = 1;
	
	enum object_type {
		no_ref_array,
		ref_array,
		class_instance
	};
	
	struct object_descriptor_t {
		object_type type;
		uint64_t size;
		uint64_t ref_map_size;
		char* ref_map;
	};
	
	object_descriptor_t NO_REF_ARRAY_DESCRIPTOR = {no_ref_array, 0, 0, nullptr};
	object_descriptor_t REF_ARRAY_DESCRIPTOR = {ref_array, 0, 0, nullptr};
	
	struct object_t {
		uint64_t flags;
		object_descriptor_t* desc;
		uint64_t size;
		union {
			object_t *next;
			object_t *new_address;
		};
		inline char is_marked(){
			return flags & gc_mask == black;
		} 
		inline void mark(){
			//std::cout << "Flags now is " << flags << "\n";
			flags = flags | gc_mask;
			//std::cout << "Marking object " << this << ", so now FLAGS equals " << flags << "\n";
		}
		inline void unmark(){
			flags = flags & ~gc_mask;
			//std::cout << "Unmarking object " << this << ", so now FLAGS equals " << flags << "\n";
		}
		inline void insert_next(object_t* next_obj){
			next_obj->next = next;
			next = next_obj;
		}
		inline void insert_next_and_mark(object_t* next_obj){
			next_obj->mark();
			insert_next(next_obj);
		}
		inline void mark_references(){
			switch(desc->type){
				case object_type::no_ref_array: break;
				case object_type::ref_array: {
					uint64_t size = get_ref_array_size();
					object_t** arr = get_ref_array();
					for(uint64_t i = 0; i < size; ++i){
						if(arr[i] != nullptr) if(!arr[i]->is_marked()) insert_next_and_mark(arr[i]);
					}
				}
				break;
				case object_type::class_instance: {
					uint64_t size = get_ref_map_size();
					char* map = get_ref_map();
					object_t** arr = (object_t**)get_data();
					for(uint64_t i = 0; i < size; ++i){
						if(map[i]) if(arr[i] != nullptr) if(!arr[i]->is_marked()) /*std::cout << "Adding " << arr[i] << " to queue" << "\n"*/ insert_next_and_mark(arr[i]);
					}
				}
				break;		
			}
		}
		inline void set_new_address(char* _new_address){
			new_address = (object_t*)_new_address;
			//std::cout << "New address of object at " << this << " is now " << new_address << "\n";
		};
		inline void resolve_references(){
			switch(desc->type){
				case object_type::no_ref_array: break;
				case object_type::ref_array: {
					uint64_t size = get_ref_array_size();
					object_t** arr = get_ref_array();
					for(uint64_t i = 0; i < size; ++i){
						if(arr[i] != nullptr) {
							//std::cout << " arr[i] = " << arr[i] << " arr[i]->new_address = " << arr[i]->new_address << "\n";
							arr[i] = arr[i]->new_address; 
						} else {
							//std::cout << arr[i] << "is null, so not resolved\n";
						}
					}
				}
				break;
				case object_type::class_instance: {
					uint64_t size = get_ref_map_size();
					char* map = get_ref_map();
					object_t** arr = (object_t**)get_data();
					for(uint64_t i = 0; i < size; ++i){
						if(map[i]) if(arr[i] != nullptr) {
							//std::cout << "arr[i] = " << arr[i] << " arr[i]->new_address = " << arr[i]->new_address << "\n";
							arr[i] = arr[i]->new_address; 
						} else {
							//std::cout << arr[i] << " is null, so not resolved\n";
						}
					}
				}
				break;		
			}
		}
		inline char* get_data(){
			return (char*)(this + 1);
		}
		inline void* get_field(uint64_t offset){
			return (void*)(get_data() + offset);
		}
		inline object_t** get_ref_array(){
			return (object_t**)(get_data() + sizeof(uint64_t));
		}
		inline uint64_t get_ref_array_size(){
			return *((uint64_t*)get_data());
		}
		inline char* get_ref_map(){
			return desc->ref_map;
		}
		inline uint64_t get_ref_map_size(){
			return desc->ref_map_size;
		}
	};
	
	inline void transfer(char* top, object_t* obj){
		uint64_t to_copy = (obj->size + sizeof(object_t));
		char* dst = top;
		char* src = (char*)obj;
		std::memmove(dst, src, to_copy);
		//for(uint64_t i = 0; *dst++ = *src++, i < to_copy; ++i);
	}
	
	class mark_and_compact_heap_t {
		char* m_memory;
		char* m_end;
		char* m_current;
		uint64_t m_count;
		object_t** m_root;
		inline char not_enough_mem(uint64_t size){
			return (m_end - m_current) < size;
		}
		void gc_mark(){
			object_t* object_queue = *m_root;
			object_queue->next = nullptr;
			object_queue->mark();
			while(object_queue != nullptr){
				//std::cout << "GC is marking references from " << object_queue << "\n";
				object_queue->mark_references();
				object_queue = object_queue -> next;
			}
		}
		void gc_evaluate_references(){
			char* curold = m_memory;
			char* curnew = m_memory;
			for(int i = 0; i < m_count; ++i){
				object_t* curobj = (object_t*)curold;
				if(curobj->is_marked()){
					//std::cout << "GC is evaluating new object address for object at " << curobj << "\n";
					curobj->set_new_address(curnew);
					curnew += (sizeof(object_t) + curobj->size);
				}
				curold += (sizeof(object_t) + curobj->size);
			}
		}
		void gc_resolve_references(){
			char* cur = m_memory;
			for(int i = 0; i < m_count; ++i){
				object_t* curobj = (object_t*)cur;
				if(curobj->is_marked()){
					//std::cout << "GC is resolving references from object at " << curobj << "\n";
					curobj->resolve_references();
				}
				cur += (sizeof(object_t) + curobj->size);
			}
		}
		void gc_compact_and_unmark(){
			char* curold = m_memory;
			char* curnew = m_memory;
			uint64_t m_new_count = 0;
			for(int i = 0; i < m_count; ++i){
				object_t* curobj = (object_t*)curold;
				uint64_t size = curobj->size + sizeof(object_t);
				if(curobj->is_marked()){
					curobj->unmark();
					transfer(curnew, curobj);
					curnew += size;
					m_new_count += 1;
				}
				curold += size;
			}
			m_current = curnew;
			m_count = m_new_count;
			//&std::cout << "New heap count: " << m_new_count << "\n";
		}
		object_t* alloc(object_descriptor_t* desc, uint64_t size){
			uint64_t fsize = size + sizeof(object_t);
			if(not_enough_mem(fsize)){
				//std::cout << "Not enough memory, GC is fired\n";
				gc();
				if(not_enough_mem(fsize)){
					std::cout << "Exception in thread \"main\" java.lang.OutOfMemoryError" << std::endl;
					exit(-1);
				}
			}
			object_t* result = (object_t*)(m_current);
			m_current += fsize; 
			std::memset((char*)result, 0, fsize);
			result->size = size;
			result->desc = desc;
			result->flags = m_count * 2;
			m_count++;
			return result;
		}
	public:
		mark_and_compact_heap_t(object_t*& root, char* memory, uint64_t length){
			m_memory = memory;
			m_end = memory + length;
			m_current = memory;
			m_root = &root;
			m_count = 0;
		}
		void gc(){
			gc_mark();
			gc_evaluate_references();
			object_t* new_root = (*m_root)->new_address; 
			gc_resolve_references();
			gc_compact_and_unmark();
			*m_root = new_root;
			//trace();
		}
		void trace(){
			object_t* cur = *m_root; uint64_t connected = 0;
			while(cur != nullptr) std::cout << cur << "\n", cur = *(object_t**)cur->get_data();
			std::cout << connected << "\n";
		}
		object_t* instantiate(object_descriptor_t* desc){
			return alloc(desc, desc->size);
		}
		object_t* instantiate_no_ref_array(uint64_t elem_size, uint64_t length){
			object_t* result = alloc(&NO_REF_ARRAY_DESCRIPTOR, sizeof(uint64_t) + elem_size * length);
			*(uint64_t*)result->get_data() = length;
			return result;
		}
		object_t* instantiate_ref_array(uint64_t length){
			object_t* result = alloc(&REF_ARRAY_DESCRIPTOR, sizeof(uint64_t) + sizeof(object_t*) * length);
			*(uint64_t*)result->get_data() = length;
			return result;
		}
	};
}; 

int main(){
	//std::cout << "This program illustrates GC on linked list example\n";
	//creating reference map for linked list node class
	char linked_list_node_ref_map[] = {1, 0};
	//creating descriptor for linked list node class
	mmngr::object_descriptor_t example;
	example.type = mmngr::object_type::class_instance;
	example.size = 16;
	example.ref_map_size = 2;
	example.ref_map = linked_list_node_ref_map;
	//std::cout << "Linked list node layout: \nclass LinkedListNode {\n    LinkedListNode next;\n    long val;\n}\n";
	//creating memory for heap
	char* heap_mem = new char[1 GB];
	//creating root pointer
	mmngr::object_t* root = nullptr;
	//creating mark and compact heap
	mmngr::mark_and_compact_heap_t heap(root, heap_mem, 1 GB);
	//allocating node
	root = heap.instantiate(&example);
	mmngr::object_t* prev = root; 
	timer stopwatch;
	//one gc cycle will occur at the end
	//it will move all objects, also objects are quite fragmented, so it is the worst case
	for(int i = 0; i < 22369621; ++i){
		mmngr::object_t* newobj = heap.instantiate(&example);
		*(mmngr::object_t**)(prev->get_data()) = newobj;
		prev = newobj;
		if(i == 0) root = newobj;
	}
	//heap.gc();
	double time = stopwatch.elapsed(); 
	std::cout << "Finished in " << time << " s\n";
	//deleting memory
	delete[] heap_mem;
}
