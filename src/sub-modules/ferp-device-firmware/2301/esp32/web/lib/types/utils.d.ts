/** Utility Types Namespace. */
declare namespace Utils_t {
    /***********************
     *  GENERIC UTILITIES  *
     ***********************/

    /** Makes some properties optional by key. */
    export type Optional<T extends object, K extends keyof T> = Extend<Omit<T, K>, Partial<Pick<T, K>>>;

    /** Coordinates a simple object extension. */
    export type Extend<A extends object, B extends object> = A & B;

    /** Either Typing. */
    export type Either<B extends boolean, T, F> = B extends true ? T : F;

    /** Base Serializable Instance. */
    export type Serializable<B extends boolean, T extends object> = {
        [K in keyof T]: B extends true ? T[K] : string;
    };

    /** Either or for promises. */
    export type Promisable<T> = T | Promise<T>;

    /**************************
     *  NAMESPACED UTILITIES  *
     **************************/

    /** Function Utilities. */
    export namespace Functor {
        /** Any available function. */
        export type Any<R extends any = any, A extends any[] = any[]> = (...args: A) => R;
    }

    /** Tuple Utilities. */
    export namespace Tuple {
        /** Maybe a Tuple typing or not. */
        export type Maybe<T> = T | Array<T>;

        /** Concatenates multiple tuples together. */
        export type Concat<T extends any[]> = T extends [infer L, ...infer R]
            ? L extends any[]
                ? [...L, ...Concat<R>]
                : L
            : T;
    }
}
