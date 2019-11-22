#include <iostream>
#include <cstdint>
#include <cstring>

#define KB * 1024

namespace mmngr
{
	const uint64_t gc_mask = 1;
	const uint64_t black = 1;
	const uint64_t white = 0;
	
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
	
	struct object_t {
		uint64_t flags;
		object_descriptor_t* desc;
		uint64_t size;
		union {
			object_t *next;
			object_t *new_adress;
		};
		inline char is_marked(uint64_t gc_toogle){
			return (char)(gc_toogle ^ (flags & gc_mask));
		} 
		inline void mark(){
			flags ^= gc_mask;
		}
		inline void insert_next(object_t* next_obj){
			next_obj->next = next;
			next = next_obj;
		}
		inline void insert_next_and_mark(object_t* next_obj){
			next_obj->mark();
			insert_next(next_obj);
		}
		inline void mark_references(uint64_t toogle){
			switch(desc->type){
				case object_type::no_ref_array: break;
				case object_type::ref_array: {
					uint64_t size = get_ref_array_size();
					object_t** arr = get_ref_array();
					for(uint64_t i = 0; i < size; ++i){
						if(arr[i] != nullptr) if(!arr[i]->is_marked(toogle)) insert_next_and_mark(arr[i]);
					}
				}
				break;
				case object_type::class_instance: {
					uint64_t size = get_ref_map_size();
					char* map = get_ref_map();
					object_t** arr = (object_t**)get_data();
					for(uint64_t i = 0; i < size; ++i){
						if(map[i]) if(arr[i] != nullptr) if(!arr[i]->is_marked(toogle)) insert_next_and_mark(arr[i]);
					}
				}
				break;		
			}
		}
		inline void set_new_adress(char* _new_adress){
			new_adress = (object_t*)_new_adress;
		};
		inline void resolve_references(){
			switch(desc->type){
				case object_type::no_ref_array: break;
				case object_type::ref_array: {
					uint64_t size = get_ref_array_size();
					object_t** arr = get_ref_array();
					for(uint64_t i = 0; i < size; ++i){
						if(arr[i] != nullptr) arr[i] = arr[i]->new_adress;
					}
				}
				break;
				case object_type::class_instance: {
					uint64_t size = get_ref_map_size();
					char* map = get_ref_map();
					object_t** arr = (object_t**)get_data();
					for(uint64_t i = 0; i < size; ++i){
						if(map[i]) if(arr[i] != nullptr) arr[i] = arr[i]->new_adress;
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
		uint64_t to_copy = obj->size + sizeof(object_t);
		char* dst = top;
		char* src = (char*)obj;
		for(uint64_t i = 0; *dst++ = *src++, i < to_copy; ++i);
	}
	
	class heap_t {
		char* m_memory;
		char* m_end;
		char* m_current;
		uint64_t m_toogle;
		uint64_t m_count;
		object_t** m_root;
		inline char not_enough_mem(uint64_t size){
			return (m_end - m_current) < size;
		}
	public:
		void gc(){
			gc_mark();
			gc_evaluate_references();
			object_t* new_root = (*m_root)->new_adress;
			gc_resolve_references();
			gc_compact();
			m_toogle ^= gc_mask;
			*m_root = new_root;
		}
	private:
		void gc_mark(){
			object_t* object_queue = *m_root;
			object_queue->next = nullptr;
			object_queue->mark();
			while(object_queue != nullptr){
				std::cout << "GC is marking object at adress 0x" << object_queue << "\n";
				object_queue->mark_references(m_toogle);
				object_queue = object_queue -> next;
			}
		}
		void gc_evaluate_references(){
			char* curold = m_memory;
			char* curnew = m_memory;
			for(int i = 0; i < m_count; ++i){
				object_t* curobj = (object_t*)curold;
				std::cout << "GC is evaluating new object address for object at " << curobj << "\n";
				if(curobj->is_marked(m_toogle)){
					curobj->set_new_adress(curnew);
					curnew += (sizeof(object_t) + curobj->size);
				}
				curold += (sizeof(object_t) + curobj->size);
			}
		}
		void gc_resolve_references(){
			char* cur = m_memory;
			for(int i = 0; i < m_count; ++i){
				object_t* curobj = (object_t*)cur;
				std::cout << "GC is resolving references from object at " << curobj << "\n";
				if(curobj->is_marked(m_toogle)){
					curobj->resolve_references();
				}
				cur += (sizeof(object_t) + curobj->size);
			}
		}
		void gc_compact(){
			char* curold = m_memory;
			char* curnew = m_memory;
			for(int i = 0; i < m_count; ++i){
				object_t* curobj = (object_t*)curold;
				uint64_t size = curobj->size + sizeof(object_t);
				std::cout << "GC is relocating object at " << curobj << " to 0x" << std::hex << (uint64_t)curnew << "\n";
				if(curobj->is_marked(m_toogle)){
					transfer(curold, curobj);
				}
				curold += size;
			}
		}
		object_t* alloc(object_descriptor_t* desc, uint64_t size){
			uint64_t fsize = size + sizeof(object_t);
			if(not_enough_mem(fsize)){
				std::cout << "Not enough memory, GC is fired\n";
				gc();
				if(not_enough_mem(fsize)){
					std::cout << "Exception in thread \"main\" java.lang.OutOfMemoryError" << std::endl;
					exit(-1);
				}
			}
			object_t* result = (object_t*)(m_current);
			m_current += fsize; 
			std::memset((char*)result, 0, fsize);
			result->flags = m_toogle; 
			result->size = size;
			result->desc = desc;
			m_count++;
			return result;
		}
	public:
		heap_t(object_t*& root, char* memory, uint64_t length){
			m_memory = memory;
			m_end = memory + length;
			m_current = memory;
			m_toogle = white;
			m_root = &root;
			m_count = 0;
		}
		object_t* instantiate(object_descriptor_t* desc){
			return alloc(desc, desc->size);
		};
	};
};

int main(){
	std::cout << "This program illustrates GC on linked list example\n";
	//creating reference map for linked list node class
	char linked_list_node_ref_map[] = {1, 0};
	//creating descriptor for linked list node class
	mmngr::object_descriptor_t example;
	example.type = mmngr::object_type::class_instance;
	example.size = 16;
	example.ref_map_size = 2;
	example.ref_map = linked_list_node_ref_map;
	std::cout << "Linked list node layout: \nclass LinkedListNode {\n    LinkedListNode next;\n    long val;\n}\n";
	//creating memory for heap
	char* heap_mem = new char[16 KB];
	//creating root pointer
	mmngr::object_t* root = nullptr;
	mmngr::heap_t heap(root, heap_mem, 16 KB);
	//creating three nodes in heap
	mmngr::object_t* node1 = heap.instantiate(&example);
	mmngr::object_t* node2 = heap.instantiate(&example);
	mmngr::object_t* node3 = heap.instantiate(&example);
	std::cout << "Address of node1 is " << node1 << '\n';
	std::cout << "Address of node2 is " << node2 << '\n';
	std::cout << "Address of node3 is " << node3 << '\n';
	//setting references
	*(mmngr::object_t**)node1->get_data() = node2;
	*(mmngr::object_t**)node2->get_data() = node3;
	*(mmngr::object_t**)node3->get_data() = nullptr;
	std::cout << "node1 now points to adress " << *(mmngr::object_t**)node1->get_data() << '\n';
	std::cout << "node2 now points to adress " << *(mmngr::object_t**)node2->get_data() << '\n';
	std::cout << "node3 now points to adress " << *(mmngr::object_t**)node3->get_data() << '\n';
	//setting root to one node
	root = node2;
	//firing gc
	heap.gc();
	//checking new adresses 
	std::cout << "Due to compaction root is now located at " << root << "\n";
	std::cout << "Due to compaction node3 is now located at " << *(mmngr::object_t**)node1->get_data() << "\n";
	//deleting memory
	delete[] heap_mem;
}
