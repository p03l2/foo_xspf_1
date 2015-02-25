#pragma once

typedef pfc::list_t<metadb_handle_ptr> dbList;


template<class T>
class xmlBaseHelper
{
	public:
		void setXmlBase( const t_size num , const char *val )
		{
			if( ( num < 0 ) || ( num >= XMLBASE_LEN ) )
			{
				console::printf( CONSOLE_HEADER"setXmlBase num error: %d" , num );
				return;
			}

			if( val == nullptr )
			{
				base[num].reset();
				return;
			}

			base[num] = val;
			return;
		}

		T getXmlBase() const
		{
			T out;
			for( const auto &i : base )
			{
				out += i;
			}
			return out;  // RVO kicks in
		}

	private:
		static const t_size XMLBASE_LEN = 4;  // playlist, trackList, track, location
		T base[XMLBASE_LEN];
};
typedef xmlBaseHelper<pfc::string8> xmlBaseImpl;

template<class T>
class lruCache
{
		struct cacheData
		{
			std::string name;
			T data;
		};

	public:
		void set( const char *in_name , const T *in_data )
		{
			// check if exist already
			for( const auto &i : cache )
			{
				if( i.name == in_name )
				{
					return;
				}
			}

			cache.push_front( { in_name , *in_data } );
			if( cache.size() > CACHE_SIZE )
				cache.pop_back();
			return;
		}

		const T *get( const char *in_name )
		{
			for( auto i = cache.cbegin() , end = cache.cend() ; i != end ; ++i )
			{
				if( i->name == in_name )
				{
					cache.splice( cache.begin() , cache , i );  // move to head
					return &cache.front().data;
				}
			}

			return nullptr;
		}

	private:
		static const t_size CACHE_SIZE = 50;
		std::list<cacheData> cache;
};
typedef lruCache<dbList> lruCacheImpl;


void open_helper( const char *p_path , const service_ptr_t<file> &p_file , playlist_loader_callback::ptr p_callback , abort_callback &p_abort );
void open_helper_location( const char *p_path , playlist_loader_callback::ptr p_callback , const tinyxml2::XMLElement *x_track , xmlBaseImpl *xml_base );
void open_helper_no_location( playlist_loader_callback::ptr p_callback , const tinyxml2::XMLElement *x_track , const dbList *in_list , lruCacheImpl *lru_cache );
void write_helper( const char *p_path , const service_ptr_t<file> &p_file , metadb_handle_list_cref p_data , abort_callback &p_abort , const bool w_location );

void addInfoHelper( const tinyxml2::XMLElement *x_parent , file_info_impl *f , const char *x_name , const char *db_name );
void filterFieldHelper( const tinyxml2::XMLElement *x_parent , const dbList *in_list , const char *x_name , const char *db_name , dbList *out , lruCacheImpl *lru_cache = nullptr );

pfc::string8 pathToUri( const char *in_path , const char *ref_path );
pfc::string8 uriToPath( const char *in_uri , const char *ref_path , const pfc::string8 base_str );

pfc::string8 urlEncodeUtf8( const char *in );
pfc::string8 urlDecodeUtf8( const char *in );
