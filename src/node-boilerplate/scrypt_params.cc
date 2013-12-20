/*
scrypt_params.cc 

Copyright (C) 2013 Barry Steyn (http://doctrina.org/Scrypt-Authentication-For-Node.html)

This source code is provided 'as-is', without any express or implied
warranty. In no event will the author be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this source code must not be misrepresented; you must not
claim that you wrote the original source code. If you use this source code
in a product, an acknowledgment in the product documentation would be
appreciated but is not required.

2. Altered source versions must be plainly marked as such, and must not be
misrepresented as being the original source code.

3. This notice may not be removed or altered from any source distribution.

Barry Steyn barry.steyn@gmail.com

*/

#include <node.h>
#include <v8.h>
#include <string>

using namespace v8;

#include "scrypt_common.h"
#include "scrypt_params.h"

//Scrypt is a C library and there needs c linkings
extern "C" {
    #include "pickparams.h"
}

namespace {

//Defaults
const size_t MAXMEM = 0;
const double MAXMEMFRAC = 0.5;

//
// Structure to hold information
//
struct TranslationInfo {
	//Async callback function
	Persistent<Function> callback;

	//Custom data
	int result;
	size_t maxmem;
	double maxmemfrac;
	double maxtime;
	int N;
	uint32_t r;
	uint32_t p;

	//Constructor / Destructor
	TranslationInfo() : maxmem(MAXMEM), maxmemfrac(MAXMEMFRAC) { callback.Clear(); }
	~TranslationInfo() { callback.Dispose(); }
};

//
// Validates JavaScript params function and determines whether it is asynchronous or synchronous
//
int 
ValidateArguments(const Arguments& args, std::string& errMessage, TranslationInfo &translationInfo) {
    if (args.Length() == 0) {
        errMessage = "Wrong number of arguments: At least one argument is needed - the maxtime";
        return 1;
    }

	if (args.Length() > 0 && args[0]->IsFunction()) {
		errMessage = "Wrong number of arguments: At least one argument is needed before the callback - the maxtime";
		return 1;
	}

    for (int i=0; i < args.Length(); i++) {
		v8::Handle<v8::Value> currentVal = args[i];
		if (i > 0 && currentVal->IsFunction()) { //An async signature
            translationInfo.callback = Persistent<Function>::New(Local<Function>::Cast(args[i]));
			return 0;
		}

        switch(i) {
            case 0:
                //Check maxtime is a number
                if (!currentVal->IsNumber()) {
                    errMessage = "maxtime argument must be a number";
                    return 1;
                }

                //Check that maxtime is not less than or equal to zero (which would not make much sense)
                translationInfo.maxtime = currentVal->ToNumber()->Value();
                if (translationInfo.maxtime <= 0) {
                    errMessage = "maxtime must be greater than 0";
                    return 1;
                }
                
                break;   

			case 1:
                //Check maxmem
				if (!currentVal->IsUndefined()) {
					if (!currentVal->IsNumber()) {
						errMessage = "maxmem argument must be a number";
						return 1;
					}

					//Set mexmem if possible, else set it to default
					if (currentVal->ToNumber()->Value() > 0) {
						translationInfo.maxmem = Local<Number>(args[i]->ToNumber())->Value();
					}
				}
 
				break;
            
			case 2:
                //Check max_memfac is a number
				if (!currentVal->IsUndefined()) {
					if (!currentVal->IsNumber()) {
						errMessage = "max_memfrac argument must be a number";
						return 1;
					}

					//Set mexmemfrac if possible, else set it to default
					if (currentVal->ToNumber()->Value() > 0) {
						translationInfo.maxmemfrac = Local<Number>(args[i]->ToNumber())->Value();
					}
				}

                break; 
        }
    }

    return 0;
}

//
// Creates the actual JSON object that will be returned to the user
//
void 
createJSONObject(Local<Object> &obj, const int &N, const uint32_t &r, const uint32_t &p) {
	obj = Object::New();
	obj->Set(String::NewSymbol("N"), Integer::New(N));
	obj->Set(String::NewSymbol("r"), Integer::New(r));
	obj->Set(String::NewSymbol("p"), Integer::New(p));
}

//
// Work funtion: Work performed here
//
void
ParamsWork(TranslationInfo* translationInfo) {
	translationInfo->result = pickparams(&translationInfo->N, &translationInfo->r, &translationInfo->p, translationInfo->maxtime, translationInfo->maxmem, translationInfo->maxmemfrac);
}

//
// Asynchronous: Wrapper to work function
//
void 
ParamsAsyncWork(uv_work_t* req) {
	ParamsWork(static_cast<TranslationInfo*>(req->data));
}

//
// Synchronous: After work function
//
Handle<Value> 
ParamsSyncAfterWork(HandleScope &scope, TranslationInfo *translationInfo) {
	Local<Object> obj;
	int result = translationInfo->result;

	if (!result) {
		createJSONObject(obj, translationInfo->N, translationInfo->r, translationInfo->p);
	}

	delete translationInfo; //cleanup
	
	if (result) { //There has been an error
        ThrowException(
			Internal::MakeErrorObject(2,"",result)
        );
        return scope.Close(Undefined());		
	} else { 
		return scope.Close(obj);
	}
}


//
// Asynchronous: After work function
//
void
ParamsAsyncAfterWork(uv_work_t* req) {
    HandleScope scope;
    TranslationInfo* translationInfo = static_cast<TranslationInfo*>(req->data);

    if (translationInfo->result) { //There has been an error
        Local<Value> err = Internal::MakeErrorObject(2,"",translationInfo->result);

        //Prepare the parameters for the callback function
        const unsigned argc = 1;
        Local<Value> argv[argc] = { err };

        // Wrap the callback function call in a TryCatch so that we can call
        // node's FatalException afterwards. This makes it possible to catch
        // the exception from JavaScript land using the
        // process.on('uncaughtException') event.
        TryCatch try_catch;
        translationInfo->callback->Call(Context::GetCurrent()->Global(), argc, argv);
        if (try_catch.HasCaught()) {
            node::FatalException(try_catch);
        }
    } else {
        const unsigned argc = 2;
		Local<Object> obj;
		createJSONObject(obj, translationInfo->N, translationInfo->r, translationInfo->p);
        
		Local<Value> argv[argc] = {
            Local<Value>::New(Null()),
            Local<Value>::New(obj)
        };

        TryCatch try_catch;
        translationInfo->callback->Call(Context::GetCurrent()->Global(), argc, argv);
        if (try_catch.HasCaught()) {
            node::FatalException(try_catch);
        }
    }

    //Clean up
    delete translationInfo;
    delete req;
}

} //unnamed namespace

//
// Params: Parses arguments and determines what type (sync or async) this function is
//         This function is the "entry" point from JavaScript land
//
Handle<Value> 
Params(const Arguments& args) {
	HandleScope scope;
	std::string validateMessage;
	TranslationInfo* translationInfo = new TranslationInfo();

	//Validate arguments and determine function type
	if (ValidateArguments(args, validateMessage, *translationInfo)) {
        ThrowException(
            Exception::TypeError(String::New(validateMessage.c_str()))
        );
		delete translationInfo;
        return scope.Close(Undefined());
	}

	if (translationInfo->callback.IsEmpty()) { 
		//Synchronous
		
		ParamsWork(translationInfo);
		return ParamsSyncAfterWork(scope, translationInfo);
	} else { 
		//Asynchronous

		//Asynchronous work request
		uv_work_t *req = new uv_work_t();
		req->data = translationInfo;

		//Schedule work request
		int status = uv_queue_work(uv_default_loop(), req, ParamsAsyncWork, (uv_after_work_cb)ParamsAsyncAfterWork);
		assert(status == 0);

		return scope.Close(Undefined());
	}
}