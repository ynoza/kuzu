const callbackWrapper = require("./common.js");

class QueryResult {
    #queryResult
    #isClosed
    constructor(queryResult) {
        this.#queryResult = queryResult;
        this.#isClosed = false;
    }

    checkForQueryResultClose(){
        if (this.#isClosed){
            throw new Error("Query Result is Closed");
        }
    }

    close(){
        if (this.#isClosed){
            return;
        }
        this.#queryResult.close();
        this.#isClosed = true;
    }

    async each(rowCallback, doneCallback, errorCallback) {
        this.checkForQueryResultClose();
        while (this.#queryResult.hasNext()) {
            await this.each().then(row => {
                rowCallback(row);
            }).catch(err => {
                errorCallback(err);
            })
        }
        doneCallback();
    }

    async each() {
        this.checkForQueryResultClose();
        return new Promise((resolve, reject) => {
            this.#queryResult.getNext((err, result) => {
                if (err) {
                    return reject(err);
                } else {
                    return resolve(result);
                }
            })
        })
    }

    all(callback = null) {
        this.checkForQueryResultClose();
        if (callback) {
            this.#queryResult.all((err, result) => {
                callbackWrapper(err, () => {
                    callback(result);
                });
            });
        } else {
            return new Promise ((resolve, reject) => {
                this.#queryResult.all((err, result) => {
                    if (err) { return reject(err); }
                    return resolve(result);
                });
            })
        }
    }
}

module.exports = QueryResult
