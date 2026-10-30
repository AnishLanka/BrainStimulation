#pragma once
#define ESP_RETURN_ON_FALSE(c,e,...) do{if(!(c))return(e);}while(0)
#define ESP_RETURN_ON_ERROR(c,...) do{int _e=(c);if(_e)return _e;}while(0)
